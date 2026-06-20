#include "device_manager.h"
#include "esp_timer.h"

static const char *TAG = "DEV_MGR";

//采集任务超时时间戳
#define OFFLINE_TIMEOUT_MS 10000


//存储设备
device_data_t device_table[MAX_DEVICES] = {0};
static SemaphoreHandle_t s_mutex = NULL;
//更新某个设备数据
void device_manager_update(uint8_t id, const device_data_t *data)
{
    if (s_mutex == NULL || data == NULL)
        return;

    //------------------------写入-----------------------------
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (device_table[i].id == id)
        {
            device_table[i] = *data;
            break;
        }
    }

    xSemaphoreGive(s_mutex);
    //------------------------写完-----------------------------
}


//获取所有在线设备数据（App Task 调用）
int device_manager_get_all(device_data_t *out_array, int max_count)
{
    if (s_mutex == NULL || out_array == NULL || max_count <= 0)
        return 0;

    int count = 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_DEVICES && count < max_count; i++)
    {
        if (device_table[i].id != 0 && device_table[i].online)
        {
            out_array[count++] = device_table[i];
        }
    }

    xSemaphoreGive(s_mutex);
    
    return count;
}


//检查设备离线
void device_manager_check_offline(void)
{
       uint32_t now = esp_timer_get_time() / 1000;  

       xSemaphoreTake(s_mutex , portMAX_DELAY);
   for (int i = 0; i < MAX_DEVICES; i++)
   {
    //空槽跳过
       if (device_table[i].id == 0)
       continue;
    //离线跳过
       if (!device_table[i].online)
       continue;
    //ts为 0代表未开采过数据
       if (device_table[i].ts == 0)
       continue;
    
    if (now - device_table[i].ts > OFFLINE_TIMEOUT_MS)
    {
        device_table[i].online = false;
        ESP_LOGW(TAG, "Dev[%d] %s offline (ts stale)",
        device_table[i].id, device_table[i].name);
    }
   }
       xSemaphoreGive(s_mutex);

}



void device_manager_init(void)
{
    //初始化设备管理器：创建 Mutex，填充模拟设备，启动模拟采集任务
    SemaphoreHandle_t xMutex = xSemaphoreCreateMutex();
    s_mutex = xMutex;
    assert(s_mutex);
    //2.清零设备表
    memset(device_table, 0, sizeof(device_table));

    //3.填充 3 个模拟设备
    device_table[0] = (device_data_t){
        .id = 1, .name = "sensor_01", .online = false,
        .ts = 0, .temp = 25.0f, .humi = 50.0f, .current = 1.2f
    };
    device_table[1] = (device_data_t){
        .id = 2, .name = "sensor_02", .online = false,
        .ts = 0, .temp = 28.0f, .humi = 45.0f, .current = 0.8f
    };
    device_table[2] = (device_data_t){
        .id = 3, .name = "sensor_03", .online = false,
        .ts = 0, .temp = 22.0f, .humi = 60.0f, .current = 1.5f
    };
    ESP_LOGI(TAG, "Device table initialized: 3 devices");
}