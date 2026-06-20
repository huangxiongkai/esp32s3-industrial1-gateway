#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>

// 初始化设备管理器：创建 Mutex，填充模拟设备，启动模拟采集任务
void device_manager_init(void);
// 更新某个设备的数据（M3 内部模拟调用，M7 Poll Scheduler 调用）
void device_manager_update(uint8_t id, const device_data_t *data);
// 获取所有在线设备数据（App Task 调用），返回实际数量
int device_manager_get_all(device_data_t *out_array, int max_count);
// 检查设备离线（M7 实现，M3 空函数占位）
void device_manager_check_offline(void);





#endif