#ifndef TYPES_H
#define TYPES_H

//定义 device_data_t 结构体（设备数据模型）

//数据类型：id ， 名称 ， 在线状态 ， 最后更新时间戳 ，【三个传感器数据】

#include <stdint.h>
#include <stdbool.h>

#define MAX_DEVICES 8
typedef struct 
{
    uint8_t id;              // 设备逻辑 ID
    char name[16];           // 设备名（如 "sensor_01"）
    bool online;             // 在线状态
    uint32_t ts;             // 最后更新时间戳（ms）
    float temp;              // 温度
    float humi;              // 湿度
    float current;           // 电流
}device_data_t;

//定义 gateway_config_t 结构体（网关配置模型）
typedef struct {
    char wifi_ssid[32];           // WiFi SSID
    char wifi_pass[64];           // WiFi 密码
    char mqtt_broker[64];         // MQTT Broker URI（如 "mqtt://mqtts.heclouds.com:1883"）
    uint16_t mqtt_port;           // MQTT 端口
    char mqtt_user[32];           // MQTT 用户名
    char mqtt_pass[256];          // MQTT 密码/Token（OneNET token 很长，用 256 字节）
    char mqtt_client_id[32];      // MQTT 客户端 ID
    uint32_t poll_interval_ms;    // Modbus 轮询周期  控制Poll scheduler多久去RS485总线上“问”一次从站设备
    uint32_t report_interval_ms;  // 数据上报周期（App Task 用）
} gateway_config_t;

// 统一错误码
typedef enum {
    ERR_OK = 0,
    ERR_TIMEOUT,
    ERR_CRC,
    ERR_OFFLINE,
    ERR_MQTT_CONN,
    ERR_WIFI_CONN,
    ERR_NVS,
} error_t;

// ==================== 系统事件枚举 ====================
typedef enum {
    SYS_EVENT_WIFI_CONNECTED = 1,    // WiFi 获得 IP
    SYS_EVENT_WIFI_DISCONNECTED,     // WiFi 断开
    SYS_EVENT_MQTT_CONNECTED,        // MQTT 连接成功
    SYS_EVENT_MQTT_DISCONNECTED,     // MQTT 断开连接
} sys_event_t;


#endif