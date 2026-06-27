#include "config_manager.h"

static const char *TAG = "CFG_MGR";

#define NVS_NAMESPACE "gw_cfg"       //NVS 命名空间
#define NVS_KEY_CFG    "cfg"          //blob 的 key
//用blob的方法一次把整个结构体写入
static gateway_config_t s_config;    //内存缓存

static const gateway_config_t DEFAULT_CONFIG = {
    .wifi_ssid          = CONFIG_EXAMPLE_WIFI_SSID,
    .wifi_pass          = CONFIG_EXAMPLE_WIFI_PASSWORD,
    .mqtt_broker        = "mqtt://mqtts.heclouds.com",
    .mqtt_port          = 1883,
    .mqtt_user          = "3uKB3k9DzN",
    .mqtt_pass          = "version=2018-10-31&res=products%2F3uKB3k9DzN%2Fdevices%2FNET_1&et=1911479884&method=md5&sign=RVxDZSdHr7uj9gQ9sMLEeA%3D%3D",
    .mqtt_client_id     = "NET_1",
    .poll_interval_ms   = 1000,
    .report_interval_ms = 2000,
};

//从NVS加载配置
void config_manager_init(void)
{
   nvs_handle_t handle;
   //1.打开命名空间，获取操作句柄
   esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) 
    {
        ESP_LOGW(TAG, "NVS open failed: %s, writing defaults", esp_err_to_name(err));
        config_manager_reset_default();
        return;
    }

   //2.尝试读取NVS读取，配置BLOB
   size_t len = sizeof(s_config);
   err = nvs_get_blob(handle, NVS_KEY_CFG, &s_config, &len);
   //防止句柄泄露,每次open了都要关闭
   nvs_close(handle);

   //返回码处理
   if (err == ESP_ERR_NVS_NOT_FOUND) 
    {
        ESP_LOGI(TAG, "No config found, writing defaults");
        config_manager_reset_default();
    } 
    else if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "Config loaded from NVS");
    } 
    else 
    {
        ESP_LOGE(TAG, "NVS read failed: %s", esp_err_to_name(err));
        config_manager_reset_default();
    }
   
}


//读取当前配置（RAM）
void config_manager_get(gateway_config_t *out)
{
    if (out) {
        memcpy(out, &s_config, sizeof(gateway_config_t));
    }
}

//写入当前配置
void config_manager_set(const gateway_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_CFG, cfg, sizeof(gateway_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write blob failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    nvs_close(handle);

    memcpy(&s_config, cfg, sizeof(gateway_config_t));
    ESP_LOGI(TAG, "Config saved to NVS");
}


void config_manager_reset_default(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    //清楚所有旧配置
    nvs_erase_all(handle);

    esp_err_t err2 = nvs_set_blob(handle, NVS_KEY_CFG, &DEFAULT_CONFIG, sizeof(gateway_config_t));
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "NVS write blob failed on reset: %s", esp_err_to_name(err2));
        nvs_close(handle);
        return;
    }

    err2 = nvs_commit(handle);
    if (err2 != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed on reset: %s", esp_err_to_name(err2));
        nvs_close(handle);
        return;
    }

    nvs_close(handle);
    memcpy(&s_config, &DEFAULT_CONFIG, sizeof(gateway_config_t));
    ESP_LOGI(TAG, "Config reset to defaults");
}


