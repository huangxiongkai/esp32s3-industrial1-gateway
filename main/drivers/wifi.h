#ifndef WIFI_H
#define WIFI_H

#include "types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @brief 初始化 WiFi STA（配置但不启动连接）
 * @param cfg 网关配置，包含 SSID 和密码
 * @return ERR_OK 成功，ERR_WIFI_CONN 初始化失败
 */
error_t wifi_driver_init(const gateway_config_t *cfg);

/**
 * @brief 启动 WiFi 连接（esp_wifi_start + esp_wifi_connect）
 * @return ERR_OK 成功，ERR_WIFI_CONN 启动失败
 */
error_t wifi_driver_start(void);

/**
 * @brief 停止 WiFi（esp_wifi_stop）
 * @return ERR_OK 成功
 */
error_t wifi_driver_stop(void);

/**
 * @brief 设置事件通知队列，WiFi 事件通过此队列告知状态机
 * @param queue 系统事件队列句柄
 */
void wifi_driver_set_event_queue(QueueHandle_t queue);

#endif