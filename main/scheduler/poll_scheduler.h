#ifndef POLL_SCHEDULER_H
#define POLL_SCHEDULER_H

/**
 * @brief 启动 Modbus 轮询调度任务
 *
 * 内部创建 FreeRTOS 任务，周期性遍历设备映射表，
 * 调用 modbus_read_device() 采集数据并更新 device_manager。
* 轮询周期从 config_manager 的 poll_interval_ms 读取。
    */
void poll_scheduler_start(void);

/**
 * @brief 停止轮询调度任务
 *
 * 设置退出标志，任务在当前轮询周期结束后自行退出。
 * 不会立刻杀死任务，避免 Mutex 泄漏。
 */
void poll_scheduler_stop(void);



#endif