#include "poll_scheduler.h"
#include "modbus_master.h"
#include "device_manager.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POLL_SCHED";

#define POLL_TASK_STACK     4096
#define POLL_TASK_PRIO      5
#define OFFLINE_THRESHOLD   3       // 连续失败 N 次判定离线

//任务运行标志位
static volatile bool s_running = false;
static TaskHandle_t s_task_handle = NULL;

//设备映射表
//设备1：温湿度传感器
static const modbus_reg_map_t s_map_dev1[] = 
{
    {
       .slave_addr = 1, .reg_start = 0x0000,  .scale = 0.1f, .field = FIELD_TEMP
    },
    {
       .slave_addr = 1, .reg_start = 0x0001,  .scale = 0.1f, .field = FIELD_HUMI
    },
};
//设备2：电流检测
static const modbus_reg_map_t s_map_dev2[] =
{
    {
        .slave_addr = 2, .reg_start = 0x0000,  .scale = 0.01f, .field = FIELD_CURRENT
    },
};
// 设备3：备用（用于验证离线检测）
static const modbus_reg_map_t s_map_dev3[] = 
{
    {
       .slave_addr = 3, .reg_start = 0x0000,  .scale = 0.1f ,  .field = FIELD_CURRENT
    },
};


//设备表：
typedef struct 
{
    uint8_t dev_id;               //id
    const char *name;             //设备名称
    const modbus_reg_map_t *map;  //映射表指针
    int map_count;                //映射条目数
} poll_device_entry_t;


static const poll_device_entry_t s_device_table[] = 
{
    { .dev_id = 1, .name = "sensor_th",  .map = s_map_dev1, .map_count = 2 },
    { .dev_id = 2, .name = "sensor_cur", .map = s_map_dev2, .map_count = 1 },
    { .dev_id = 3, .name = "sensor_bak", .map = s_map_dev3, .map_count = 1 },
};

#define DEVICE_COUNT  (sizeof(s_device_table) / sizeof(s_device_table[0]))
//每个设备的连续失败计数
static uint8_t s_fail_count[DEVICE_COUNT] = {0};

/**
 * @brief 轮询任务主体
 *
 * 每个周期：遍历设备表 → modbus_read_device → 更新设备管理器
 * 连续失败达阈值 → 标记离线；恢复成功 → 清零计数
*/
static void poll_task(void *arg)
{
    //1.读取NVS中的轮询时间
    gateway_config_t cfg;
    config_manager_get(&cfg);
    uint32_t interval_ms = cfg.poll_interval_ms;  //轮询周期
    //未写入时为0
    if (interval_ms == 0)
    {
        interval_ms = 1000;
    }
        ESP_LOGI(TAG, "Started, interval=%lu ms, devices=%d",
                (unsigned long)interval_ms,(int)DEVICE_COUNT);
    while (s_running)
    {
        for (int i = 0; i < (int)DEVICE_COUNT; i++)
        {
            if (!s_running)
            break;

            //2.取出结构体内的数据
            device_data_t data;
            error_t err = modbus_read_device(s_device_table[i].dev_id ,
                                             s_device_table[i].map,
                                             s_device_table[i].map_count, 
                                             &data);

            if (err == ERR_OK)
            {
                //3.更新设备管理器
                data.ts = (uint32_t)(esp_timer_get_time() / 1000);//更新时间戳
                snprintf(data.name, sizeof(data.name),"%s",s_device_table[i].name);
                device_manager_update(data.id , &data);

                    if (s_fail_count[i] > 0) 
                    {
                        ESP_LOGI(TAG, "Device %d [%s]recovered",data.id,s_device_table[i].name);
                    } 
                    s_fail_count[i] = 0;
            }
            else
            {
                s_fail_count[i]++;
                ESP_LOGI(TAG , "Device %d [%s] fail (%d)(%d) , err = %d",
                        s_device_table[i].dev_id,
                        s_device_table[i].name,
                        s_fail_count[i],
                        OFFLINE_THRESHOLD,
                        err);

                if(s_fail_count[i] >= OFFLINE_THRESHOLD)
                {
                    //达到阈值，设备下线
                    device_data_t offline_data = {0};
                    offline_data.id     = s_device_table[i].dev_id;
                    offline_data.online = false;
                    snprintf(offline_data.name,sizeof(offline_data.name),
                                          "%s",s_device_table[i].name);

                    device_manager_update(offline_data.id,&offline_data);
                    if (s_fail_count[i] == OFFLINE_THRESHOLD)
                    {
                        ESP_LOGE(TAG, "Device %d [%s]OFFLINE",offline_data.id,s_device_table[i].name);
                    }
                }
            }
            //给从站处理时间
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
        ESP_LOGI(TAG, "Task exiting");
        s_task_handle = NULL;
        vTaskDelete(NULL);
    
}

//轮询任务启动
void poll_scheduler_start(void)
{
    if (s_task_handle != NULL) return; 
   s_running = true;
   xTaskCreate(poll_task , "poll_sched" , POLL_TASK_STACK , NULL , 
               POLL_TASK_PRIO , &s_task_handle);
}

void poll_scheduler_stop(void)
{
    if (s_task_handle == NULL) return; 
    s_running = false;
    ESP_LOGI(TAG, "Stop requested");
}