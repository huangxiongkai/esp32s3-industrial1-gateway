#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ==================== 系统状态枚举 ====================
typedef enum {
    STATE_INIT,         // 系统初始化（NVS、配置、事件循环）
    STATE_WIFI_CONN,    // 等待 WiFi 连接
    STATE_MQTT_CONN,    // 等待 MQTT 连接
    STATE_RUNNING,      // 正常运行（所有任务已启动）
} system_state_t;
// ==================== 公共接口 ====================
/**
 * @brief 启动状态机（内部创建任务，不返回）
 */
void state_machine_run(void);


#endif