#include "cache_task.h"
#include "mqtt_app.h"
#include "flash_storage.h"
#include "app_msg.h"
#include "config_manager.h"

#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "CACHE_TASK";

#define CACHE_TASK_STACK        4096     
#define CACHE_TASK_PRIO         5       
#define QUEUE_WAIT_MS           200     // 取消息超时
#define PUBLISH_GAP_MS          1000    // 任意两次发布的最小间隔

static QueueHandle_t s_mqtt_queue = NULL;
static TaskHandle_t s_task_handle = NULL;

//线程运行标志位
static volatile bool s_running = false;
static volatile bool s_online = false;
//自旋锁
static portMUX_TYPE s_spin = portMUX_INITIALIZER_UNLOCKED;
static gateway_config_t s_cfg;

static int64_t s_last_publish_ms = 0;   // 上次发布时间戳（ms）
static uint32_t s_ovf_id = 0;           // 溢出上报的消息 id 递增器

//设置设备在线状态
void cache_task_set_online(bool online)
{
    taskENTER_CRITICAL(&s_spin);
    s_online = online;
    taskEXIT_CRITICAL(&s_spin);
}

//检查上次发布是否已过最小间隔
static bool publish_gap_ok(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    return (now - s_last_publish_ms) >= PUBLISH_GAP_MS;
}

//记录本次时间戳
static void publish_stamp(void)
{
    s_last_publish_ms = esp_timer_get_time() / 1000;
}


error_t cache_task_start(QueueHandle_t mqtt_queue)
{
    if (mqtt_queue == NULL)
    {
        return ERR_MQTT_CONN;
    }
    //防重入
     if (s_task_handle != NULL)
    {
        return ERR_OK;
    }

    s_mqtt_queue = mqtt_queue;
    config_manager_get(&s_cfg);

    //开始运行
    s_running = true;
    if (xTaskCreate(cache_task, "cache_task", CACHE_TASK_STACK,
                    NULL, CACHE_TASK_PRIO, &s_task_handle) != pdPASS)
    {
        s_running = false;   //回滚标志，保持状态一致
        s_task_handle = NULL;
        return ERR_MQTT_CONN;
    }
    return ERR_OK;
}

 /**
 * @brief 停止 cache_task（退出标志 + 任务自删，不硬删）
 * @note M5 教训：vTaskDelete 硬删不释放资源且可能留半截状态，
 *       正确姿势是置标志让任务善终，轮询等待 + 超时兜底
 */
void cache_task_stop(void)
{
    if (s_task_handle == NULL)
    {
        return;
    }
    s_running = false;

     //等任务自删(3s)
    for (int i = 0; i < 30 && s_task_handle != NULL; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    //任务卡死处理
    if (s_task_handle != NULL)
    {
        ESP_LOGW(TAG, "stop timeout, force delete");
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
}


/**
 * @brief  上线上升沿时上报满溢计数（一次性）
 * @note   publish 失败必须 giveback 还回去，
 *         否则计数永久丢失；topic 按 OneNET 物模型 property/post 拼接
 */
static void report_overflow(void)
{
    //取出溢出次数
    uint32_t ovf = flash_storage_take_overflow();
    if (ovf == 0)
    {
        return;   
    }

    char topic[MSG_TOPIC_MAX];
    char payload[MSG_PAYLOAD_MAX];

    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
                s_cfg.mqtt_user, s_cfg.mqtt_client_id);
    snprintf(payload, sizeof(payload),
                "{\"id\":\"%lu\",\"version\":\"1.0\","
                "\"params\":{\"overflow_count\":{\"value\":%lu}}}",
                (unsigned long)++s_ovf_id, (unsigned long)ovf);

    if (mqtt_app_publish(topic, payload) == ERR_OK)
    {
        publish_stamp();   //占用了一次发布配额，打时间戳
        ESP_LOGW(TAG, "overflow reported: %lu entries lost during offline",
                    (unsigned long)ovf);
    }
    else
    {
        flash_storage_giveback_overflow(ovf);   //失败还回去，下次上升沿重试
        ESP_LOGW(TAG, "overflow report failed, %lu given back",
                    (unsigned long)ovf);
    }
}

/**
 * @brief  cache_task 主循环：store-and-forward 分流器
 * @note   四职责：取消息 → 在线分流 → 上升沿检测 → 限速补传
 *         限速不靠 vTaskDelay 死等，靠 publish_gap_ok() 时间戳比较，
 *         保证实时消息永不被补传阻塞
 */
static void cache_task(void *pvParameters)
{
    app_msg_t msg;                                  //640B，接收实时消息
    uint8_t back_buf[CACHE_DATA_MAX + 1];           //补传缓冲，+1 放 '\0'
    uint16_t back_len = 0;
    uint32_t back_ts = 0;
    bool last_online = false;                       //边沿检测基准

    while (s_running)
    {
        //1.取消息
        if (xQueueReceive(s_mqtt_queue, &msg,pdMS_TO_TICKS(QUEUE_WAIT_MS)) == pdTRUE)
        {
            bool online;
            //传入online状态
            taskENTER_CRITICAL(&s_spin);
            online = s_online;
            taskEXIT_CRITICAL(&s_spin);

            uint16_t plen = (uint16_t)strlen(msg.payload);

            //发布成功
            if (online && mqtt_app_publish(msg.topic, msg.payload) == ERR_OK)
            {
                publish_stamp();                    // 实时流也占限流配额
            }
            else
            {
                // 离线，或在线但发布失败
                flash_storage_write((const uint8_t *)msg.payload, plen);
            }
        }

        //2.边沿检测
        bool online_now;   //现在的在线状态
        taskENTER_CRITICAL(&s_spin);
        online_now = s_online;
        taskEXIT_CRITICAL(&s_spin);

        //上报溢出状况处理
        if (online_now && !last_online)
        {
            ESP_LOGI(TAG, "cache online, pending=%d",
                        flash_storage_pending_count());
            report_overflow();                      // 一次性交代满溢计数
        }
        else if (!online_now && last_online)
        {
            ESP_LOGW(TAG, "cache offline");
        }
        last_online = online_now;

        //3.限速补传
        if (online_now && flash_storage_pending_count() > 0 && publish_gap_ok())
        {
            if (flash_storage_read(back_buf, sizeof(back_buf) - 1,
                                    &back_len, &back_ts) == 0)
            {
                back_buf[back_len] = '\0';

                char topic[MSG_TOPIC_MAX];
                snprintf(topic, sizeof(topic),
                            "$sys/%s/%s/thing/property/post",
                            s_cfg.mqtt_user, s_cfg.mqtt_client_id);

                if (mqtt_app_publish(topic, (const char *)back_buf) == ERR_OK)
                {
                    publish_stamp();                //更新上次上报时间，触发限流规则
                    flash_storage_mark_sent();      //推进读指针，标记已发送
                    if (flash_storage_pending_count() == 0)
                    {
                        ESP_LOGW(TAG, "[BACKFILL] all done, cache empty");
                    }
                    else
                    {
                        ESP_LOGI(TAG, "[BACKFILL] resent entry, len=%u", back_len);
                    }
                }
                //失败，不动读指针，下轮重试——at-least-once
            }
        }
    }
    
    //自动销毁
    s_task_handle = NULL;
    vTaskDelete(NULL);
}