idf_component_register(SRCS "app_main.c"
                            "app/app_task.c"
                            "app/state_machine.c"
                            "app/cache_task.c"
                            "manager/device_manager.c"
                            "common/config_manager.c"
                            "protocol/mqtt_app.c"
                            "protocol/modbus_crc.c"
                            "protocol/modbus_master.c"
                            "drivers/wifi.c"
                            "drivers/rs485.c"
                            "drivers/w25q64.c"
                            "scheduler/poll_scheduler.c"
                            "storage/flash_storage.c"
                       INCLUDE_DIRS "."
                                    "app"
                                    "common"
                                    "manager"
                                    "protocol"
                                    "drivers"
                                    "scheduler"
                                    "storage"
                       REQUIRES mqtt
                                json
                                nvs_flash
                                esp_wifi
                                esp_event
                                esp_netif
                                driver)
