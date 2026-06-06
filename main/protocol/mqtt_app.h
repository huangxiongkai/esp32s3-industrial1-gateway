#ifndef MQTT_APP_H
#define MQTT_APP_H

#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "types.h"
#include "common/config_manager.h"

error_t mqtt_app_start(const gateway_config_t *cfg, QueueHandle_t sys_event_queue);
error_t mqtt_app_stop(void);

/**
 * @brief 同步发布一条消息（供 cache_task 调用）
 * @param topic   发布主题
 * @param payload 消息内容（以 '\0' 结尾的 JSON 字符串）
 * @return ERR_OK 发布已受理；ERR_MQTT_CONN 客户端未就绪或发布失败
 * @note QoS1。返回 ERR_OK 只代表协议栈受理，云端 ACK 由 esp_mqtt 内部处理
 */
error_t mqtt_app_publish(const char *topic, const char *payload);

#endif