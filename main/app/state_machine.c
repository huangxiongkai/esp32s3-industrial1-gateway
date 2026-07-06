#include "state_machine.h"
#include "app_task.h"
#include "config_manager.h"
#include "device_manager.h"
#include "mqtt_app.h"
#include "wifi.h"
#include "esp_log.h"
#include "poll_scheduler.h"

#include "esp_netif.h"      
#include "esp_event.h"      
#include "esp_system.h"     

#include "rs485.h"
#include "cache_task.h"

static const char *TAG = "STATE_MACHINE";

// 事件队列容量
#define EVT_QUEUE_DEPTH  10
// WiFi 重试间隔
#define WIFI_RETRY_DELAY_MS  5000
// MQTT 重试间隔（MQTT 连得快，间隔短一些）
#define MQTT_RETRY_DELAY_MS  3000

// 系统事件队列——wifi 和 mqtt 都往这里发事件，状态机从这里收
static QueueHandle_t s_event_queue = NULL;
// MQTT 数据队列——app_task 把 JSON 发到这里，mqtt_publish_task 从这里收
static QueueHandle_t s_mqtt_queue = NULL;
// 配置缓存——从 config_manager 读出来后存到这里，避免每次都调 config_manager_get
static gateway_config_t s_cfg;
// 状态机当前状态——初始为 STATE_INIT
static system_state_t s_current_state = STATE_INIT;


/**
 * @brief 切换状态并打日志
 * 
 * state_machine_task 的主循环里，每个"当前状态"处理函数结束前
 * 都会调一次 change_state() 切到下一个状态
 */
static void change_state(system_state_t new_state)
{
    //与system_state_t 枚举定义一致
    const char *names[] = {"INIT", "WIFI_CONN", "MQTT_CONN", "RUNNING"};
    
    ESP_LOGI(TAG, "State: %s -> %s",               //打印状态切换日志
             names[s_current_state], names[new_state]);
    s_current_state = new_state;                    //更新当前状态
}

/**
 * @brief 清空事件队列中所有残留事件
 * 
 * 调用场景：wifi_driver_stop() 之后。
 * 因为 esp_wifi_stop() 会触发 WIFI_EVENT_STA_DISCONNECTED，
 * 如果不清理，状态机回到 WIFI_CONN 后可能立刻收到这条残留事件，造成误判。
 * 
 */
static void drain_event_queue(void)
{
    sys_event_t ev;
    while (xQueueReceive(s_event_queue, &ev, 0) == pdTRUE) {}
}

//
static void handle_init(void)
{
    //1.初始化网络接口和事件循环
    //    esp_netif_init() 初始化 TCP/IP 协议栈
    //    esp_event_loop_create_default() 创建系统事件任务
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //2.创建系统事件队列
    //    这个队列是状态机的核心——wifi 和 mqtt 的事件都经过这里
    s_event_queue = xQueueCreate(EVT_QUEUE_DEPTH, sizeof(sys_event_t));
    assert(s_event_queue);   // 队列创建失败就停在这里（内存不够）

    //3.485初始化
    rs485_init();
    ESP_LOGI(TAG, "RS485 initialized");

    //4.从 config_manager 读取配置到缓存
    config_manager_get(&s_cfg);

    //5. 初始化 WiFi 驱动
    //    配置 STA 模式、SSID/密码，但不启动连接
    error_t err = wifi_driver_init(&s_cfg);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "WiFi driver init failed: %d", err);
        esp_restart();   // 致命错误，重启系统
    }

    //6.把事件队列注入 WiFi 驱动
    //  wifi 事件处理函数拿到这个句柄后，才能把事件发给我们
    wifi_driver_set_event_queue(s_event_queue);

    //7.初始化设备管理器（创建设备表 + 启动模拟采集任务）
    device_manager_init();

    //8.创建 MQTT 数据上报队列
    //  app_task 把 JSON 发到这里，mqtt publish 任务从这里收
    s_mqtt_queue = xQueueCreate(5, sizeof(app_msg_t));
    assert(s_mqtt_queue);

    //9.启动断网续传
    if (cache_task_start(s_mqtt_queue) != ERR_OK)
    {
        ESP_LOGE(TAG, "cache_task start failed, offline data will be lost");
    }


    //10.启动采集与业务任务
    //   启动一次永不停止——断网期间继续产数据交给 cache_task 落盘
    poll_scheduler_start();
    app_task_start(s_mqtt_queue);

    //11.初始化完成，进入 WiFi 连接状态
    change_state(STATE_WIFI_CONN);
}

static void handle_wifi_conn(void)
{
    //1.发起连接
    //    wifi_driver_start() 内部调 esp_wifi_start() + esp_wifi_connect()
    error_t err = wifi_driver_start();
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "WiFi start failed: %d, retrying...", err);
        vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
        return;     //回到state_machine_task 的主循环，下一轮会再次进入此状态
    }

    //2.进入等待循环：等 CONNECTED 事件
    sys_event_t ev;
    while (1) 
    {
        //阻塞等待 5 秒，超时没收到事件就说明连接异常
        if (xQueueReceive(s_event_queue, &ev,
                          pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS)) == pdTRUE)
        {
            if (ev == SYS_EVENT_WIFI_CONNECTED) {
                //WiFi 连上了，进入下一个状态
                change_state(STATE_MQTT_CONN);
                return;
            }
            //收到 DISCONNECTED：信号抖动或路由器问题
            //不清除 s_event_queue 中残留事件，避免影响下次接收
            ESP_LOGW(TAG, "WiFi disconnected during connection");
            drain_event_queue();
        }

        //3.超时或断连后：重启 WiFi 连接
        ESP_LOGW(TAG, "WiFi timeout or disconnected, reconnecting...");
        wifi_driver_stop();
        drain_event_queue();          //清掉stop触发的DISCONNECTED事件
        vTaskDelay(pdMS_TO_TICKS(1000));  //等待一秒再重连

        if (wifi_driver_start() != ERR_OK) 
        {
            ESP_LOGW(TAG, "WiFi restart failed, will retry after delay");
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
        }
        //继续下一次while循环，等待CONNECTED事件
    }
}

//MQTT连接处理任务
static void handle_mqtt_conn(void)
{
    //1.启动 MQTT 客户端
    //  传入数据队列、配置、系统事件队列（用于通知状态机连接/断开）
    error_t err = mqtt_app_start(&s_cfg, s_event_queue);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "MQTT start failed: %d, retrying...", err);
        vTaskDelay(pdMS_TO_TICKS(MQTT_RETRY_DELAY_MS));
        return;
    }

    //2.进入等待循环
    sys_event_t ev;
    while (1) 
    {
        if (xQueueReceive(s_event_queue, &ev,
                          pdMS_TO_TICKS(MQTT_RETRY_DELAY_MS)) == pdTRUE)
        {
            if (ev == SYS_EVENT_MQTT_CONNECTED) 
            {
                //通知缓存层转直发模式
                cache_task_set_online(true);
                change_state(STATE_RUNNING);
                return;
            }
            if (ev == SYS_EVENT_WIFI_DISCONNECTED) 
            {
                //WiFi断开，直接回退
                ESP_LOGW(TAG, "WiFi lost during MQTT connect, falling back");
                mqtt_app_stop();
                change_state(STATE_WIFI_CONN);
                return;
            }
        }

        //3.超时没连上 MQTT则重试
        ESP_LOGW(TAG, "MQTT connect timeout, retrying...");
        mqtt_app_stop();
        drain_event_queue();
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (mqtt_app_start(&s_cfg, s_event_queue) != ERR_OK) {
            ESP_LOGW(TAG, "MQTT restart failed, will retry after delay");
            vTaskDelay(pdMS_TO_TICKS(MQTT_RETRY_DELAY_MS));
        }
    }
}

//状态回退处理
static void handle_running(void)
{
    sys_event_t ev;
    while (1) {
        //正常运行中，永久阻塞等事件
        if (xQueueReceive(s_event_queue, &ev, portMAX_DELAY) == pdTRUE) {
            
            if (ev == SYS_EVENT_WIFI_DISCONNECTED) {
                //WiFi 断了 → MQTT 必然也断了 → 一步回退到 WIFI_CONN
                ESP_LOGW(TAG, "WiFi lost in RUNNING, falling back to WIFI_CONN");
                cache_task_set_online(false);      //缓存层转落盘模式，采集继续
                mqtt_app_stop();             //停 MQTT
                drain_event_queue();          //清残留
                change_state(STATE_WIFI_CONN);
                return;
            }
            
            if (ev == SYS_EVENT_MQTT_DISCONNECTED) {
                //MQTT断了 → 回退到 MQTT_CONN 重连
                ESP_LOGW(TAG, "MQTT lost in RUNNING, falling back to MQTT_CONN");
                cache_task_set_online(false);      //缓存层转落盘模式，采集继续
                mqtt_app_stop();
                drain_event_queue();
                change_state(STATE_MQTT_CONN);
                return;
            }
        }
    }
}


static void state_machine_task(void *arg)
{
    while (1) {                              
        switch (s_current_state) {
        case STATE_INIT:      handle_init();       break;
        case STATE_WIFI_CONN: handle_wifi_conn();  break;  
        case STATE_MQTT_CONN: handle_mqtt_conn();  break;  
        case STATE_RUNNING:   handle_running();    break;  
        }
    }
}


void state_machine_run(void)
{
    //1.创建状态机任务，优先级 6（高于 app_task 的 5）
    xTaskCreate(state_machine_task, "state_machine", 4096, NULL, 6, NULL);
    
    //2.让出 CPU，永不返回
    vTaskDelay(portMAX_DELAY);
}


