#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "types.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>


//启动时一次调用，从 NVS 读取配置到 RAM，后续 get/set 都是操作 RAM
void config_manager_init(void);
//读取当前配置（RAM）
void config_manager_get(gateway_config_t *out);
//写入当前配置（RAM），并写入 NVS
void config_manager_set(const gateway_config_t *cfg);
//恢复默认配置（RAM），并写入 NVS
void config_manager_reset_default(void);


#endif