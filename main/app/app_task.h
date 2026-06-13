#ifndef APP_TASK_H
#define APP_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "app_msg.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#include "manager/device_manager.h"
#include "common/config_manager.h"

TaskHandle_t app_task_start(QueueHandle_t mqtt_queue);
void app_task_stop(TaskHandle_t *handle);

#endif