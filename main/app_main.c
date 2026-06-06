#include <stdio.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "config_manager.h"
#include "state_machine.h"
#include "w25q64.h"
#include "flash_storage.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup.. IDF: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_ERROR_CHECK(nvs_flash_init());
    config_manager_init();
    flash_storage_init();
    state_machine_run();
}