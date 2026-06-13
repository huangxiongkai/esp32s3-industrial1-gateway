#include "app_task.h"


//生成JSON并发送到队列，等待MQTT Task取出，送去cache_task做分支判断【采集封装】

static const char *TAG = "APP_TASK";


static QueueHandle_t s_mqtt_queue = NULL;
//程序运行标志位
static volatile bool s_app_task_running = false;
static gateway_config_t s_cfg;

/**
 * @brief  模拟采集数据的任务
 */
static void app_task(void *pvParameters)
{
    device_data_t devices[MAX_DEVICES];
    static uint32_t msg_id = 0;   //递增消息 id
    while (s_app_task_running)
    {
        //清零数组，防止残留垃圾
        memset(devices, 0, sizeof(devices));

        //从device中读取所有在线数据
        int count = device_manager_get_all(devices , MAX_DEVICES);
        if (count == 0)  //不在线则跳过本轮
            {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

        //封装 OneNET 物模型 JSON
        //建一条合并 JSON
        cJSON *root = cJSON_CreateObject();
        //消息递增计数器（）
        char id_str[14];
        snprintf(id_str, sizeof(id_str), "%lu", (unsigned long)++msg_id);
        //添加 id、version 
        cJSON_AddStringToObject(root, "id", id_str);
        cJSON_AddStringToObject(root, "version", "1.0");
        //添加嵌套对象
        cJSON *params = cJSON_AddObjectToObject(root, "params");
         if (params == NULL)  //添加失败（堆内存耗尽）
        {
            ESP_LOGE(TAG, "cJSON_AddObjectToObject failed");
            cJSON_Delete(root);
            root = NULL;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        //遍历设备，逐个封装JSON并上报
        for(int i = 0; i < count; i++)
        {
            // 浮点 raw*scale 会产生 0.51999998 这类值，云端按步长校验必拒（code 2254）。
            // 用 snprintf 定精度格式化 + AddRawToObject 原样嵌入，保证 JSON 里是干净小数。
            char num[16];

            if (devices[i].id == 1)
            {
                cJSON *temp_obj = cJSON_AddObjectToObject(params, "temperature");
                snprintf(num, sizeof(num), "%.1f", (double)devices[i].temp);
                cJSON_AddRawToObject(temp_obj, "value", num);

                cJSON *humi_obj = cJSON_AddObjectToObject(params, "humidity");
                snprintf(num, sizeof(num), "%.1f", (double)devices[i].humi);
                cJSON_AddRawToObject(humi_obj, "value", num);
            }
            else if (devices[i].id == 2)
            {
                cJSON *current_obj = cJSON_AddObjectToObject(params, "current");
                snprintf(num, sizeof(num), "%.2f", (double)devices[i].current);
                cJSON_AddRawToObject(current_obj, "value", num);
            }
            //其他设备待增加
        }
            // 将 JSON 对象转换为字符串
            char *json_str = cJSON_PrintUnformatted(root);
            if (json_str == NULL)   //分配大块字符串内存时失败
            {
                ESP_LOGE(TAG, "cJSON_PrintUnformatted failed");
                cJSON_Delete(root);
                root = NULL;
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            // 3. 填入消息结构体，发送到队列
            app_msg_t msg;
            snprintf(msg.topic, sizeof(msg.topic),
                    "$sys/%s/%s/thing/property/post",
                    s_cfg.mqtt_user, s_cfg.mqtt_client_id);
            // 将 JSON 字符串复制到 payload 中，并限制最大长度
            strncpy(msg.payload, json_str, MSG_PAYLOAD_MAX - 1);
            msg.payload[MSG_PAYLOAD_MAX - 1] = '\0';

            //传到队列
            if (xQueueSend(s_mqtt_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) 
            {
                ESP_LOGW(TAG, "Queue full, message dropped");
            } else 
            {
                ESP_LOGI(TAG, "[MQTT_TX] Sent %d devices in one JSON", count);
            }

            //清理资源
            //cJSON_PrintUnformatted  返回的字符串需要释放
            free(json_str);
            cJSON_Delete(root);  
            root = NULL;   // 防野指针
            json_str = NULL;
        
        // 你的业务逻辑
        vTaskDelay(pdMS_TO_TICKS(2000)); // 延时 2s
    }
    vTaskDelete(NULL);
}


//接收上层传下来的资源（队列）
TaskHandle_t app_task_start(QueueHandle_t mqtt_queue)
{
    //把队列句柄存到全局变量
    s_mqtt_queue = mqtt_queue;
    //获取结构体初始配置
    config_manager_get(&s_cfg);
    s_app_task_running = true;
    TaskHandle_t handle = NULL;
    xTaskCreate(app_task, "app_task", 4096, NULL, 5, &handle);
    return handle;
}

void app_task_stop(TaskHandle_t *handle)
{
    if (handle == NULL || *handle == NULL) return;
    s_app_task_running = false;

    //轮询，等待该线程被移除
    for (int i = 0; i < 20 && eTaskGetState(*handle) != eDeleted; i++) 
    {
       vTaskDelay(pdMS_TO_TICKS(100));
    }
    //2s后还没退出则强制删除
    if (eTaskGetState(*handle) != eDeleted) 
    {
        ESP_LOGW(TAG, "App task force deleted");
        vTaskDelete(*handle);
    }
    //防止悬空指针
    *handle = NULL;
}

