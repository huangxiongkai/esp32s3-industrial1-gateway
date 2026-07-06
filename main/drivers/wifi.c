#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_DRV";

static QueueHandle_t s_event_queue = NULL;   //事件通知队列（由状态机注入）
static esp_netif_t *s_sta_netif = NULL;      //WiFi STA 网络接口句柄

//==================== 事件处理函数 ====================
//注册到系统事件循环，在"系统事件任务"上下文中被调用
//绝不阻塞，只做一件事：往队列发消息通知状态机
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    //wifi驱动事件
    if (base == WIFI_EVENT) 
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_DISCONNECTED:
            /* 触发条件：
             * 1.路由关了/信号丢失/密码错
             * 2.对端主动断开
             * 3.调用 esp_wifi_stop() 主动停止
             */

            ESP_LOGW(TAG, "WiFi disconnected");
            if (s_event_queue) {
                sys_event_t ev = SYS_EVENT_WIFI_DISCONNECTED;
                xQueueSend(s_event_queue, &ev, 0);  //满了则丢丢
            }
            break;
        
        default:
        //其他WiFi事件
            break;
        }
    }
    else if (base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
            /* 触发条件：DHCP 成功获取到 IP 地址
             */
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            if (s_event_queue) 
            {
                sys_event_t ev = SYS_EVENT_WIFI_CONNECTED;
                xQueueSend(s_event_queue, &ev, 0); 
        
            }
            break;

            default:
            break;
        }
    }
}

void wifi_driver_set_event_queue(QueueHandle_t queue)
{
    s_event_queue = queue;
}

error_t wifi_driver_start(void)
{
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(err));
        return ERR_WIFI_CONN;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
        return ERR_WIFI_CONN;
    }
    ESP_LOGI(TAG, "WiFi connecting...");
    return ERR_OK;
}

error_t wifi_driver_stop(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi stop failed: %s", esp_err_to_name(err));
        return ERR_WIFI_CONN;
    }
    ESP_LOGI(TAG, "WiFi stopped");
    return ERR_OK;
}


error_t wifi_driver_init(const gateway_config_t *cfg)
{
   //1.创建默认 WiFi STA 网络接口
   s_sta_netif = esp_netif_create_default_wifi_sta();

   //2.初始化 WiFi 驱动结构体（分配资源）
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wifi_init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return ERR_WIFI_CONN;
    }

    //3.注册事件处理函数
    //WIFI_EVENT：监听 WiFi 驱动层事件
    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WIFI_EVENT register failed: %s", esp_err_to_name(err));
        return ERR_WIFI_CONN;
    }
    //IP_EVENT：监听 IP 协议栈事件（拿到 IP）
    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IP_EVENT register failed: %s", esp_err_to_name(err));
        return ERR_WIFI_CONN;
    }

    //4.配置 STA 模式和 SSID/密码
    //  只配置，不启动（状态机在 WIFI_CONN 状态调 start）
    //查看WIFI模式是否设置成功
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, cfg->wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, cfg->wifi_pass, sizeof(wifi_cfg.sta.password) - 1);
    //查看STA配置是否成功
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    ESP_LOGI(TAG, "WiFi STA configured: SSID=%s", cfg->wifi_ssid);
    return ERR_OK;
}