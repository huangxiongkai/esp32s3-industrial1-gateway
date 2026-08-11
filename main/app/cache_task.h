#ifndef CACHE_TASK_H
#define CACHE_TASK_H
 
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "types.h"

/**
 * @brief 启动 cache_task（store-and-forward 枢纽）
 * @param mqtt_queue app_task 的消息队列（state_machine 创建并注入）
 * @return ERR_OK 成功；ERR_MQTT_CONN 队列无效
 */
error_t cache_task_start(QueueHandle_t mqtt_queue);

/**
 * @brief 停止 cache_task（退出标志 + 任务自删，不用 vTaskDelete 硬删）
 */
void cache_task_stop(void);

/**
 * @brief 设置在线状态，由 state_machine 在状态迁移时调用（Step 5 接线）
 * @param online true=进入 RUNNING；false=离开 RUNNING
 * @note 临界区保护：本函数在 state_machine 任务（优先级 6）执行，
     *       cache_task（优先级 5）同时在读 s_online
 */
void cache_task_set_online(bool online);


static void cache_task(void *pvParameters);

#endif