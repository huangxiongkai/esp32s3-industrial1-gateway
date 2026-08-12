# ESP32-S3 工业边缘网关

基于 ESP-IDF v5.1.2 的工业物联网网关：Modbus RTU 从站数据采集 → MQTT 上报 OneNET 云平台，
具备状态机自愈、W25Q64 断网续传（掉电可恢复）能力。

## 核心特性

- **Modbus RTU 主站采集**：表驱动寄存器映射，支持多从站、缩放因子换算、连续失败计数离线检测
- **断网续传**：断网数据写入 W25Q64（8MB 环形缓冲，32768 条），恢复在线后按 1s/条限速补传
- **掉电安全**：三态 magic 扫描恢复 + 半写条目自愈 + 失败自动重传，断电后可恢复未满溢的缓存数据
- **满溢可见**：缓存写满按扇区覆盖最旧数据，丢失条数持久化到 NVS 并在上线时上报云端（overflow_count）
- **状态机自愈**：INIT → WIFI_CONN → MQTT_CONN → RUNNING 四状态，WiFi/MQTT 断连自动分级回退、无限重试
- **配置持久化**：WiFi/MQTT 参数存 NVS，断电不丢，支持恢复出厂

## 架构总览

```
poll_scheduler ──> modbus_master ──> RS485 ──> 从站设备
       │                                          │
       v                                          v
device_manager（设备表 + 离线检测）<── CRC校验通过的响应
       │
       v
app_task（cJSON 合并封装，0.5 条/秒）──> mqtt_queue ──> cache_task（分流闸门）
                                                          │
                                         ┌── 在线 ──> mqtt_app_publish ──> OneNET
                                         └── 离线 ──> flash_storage ──> W25Q64
恢复在线：flash_storage_read ──> publish ──> mark_sent（1s/条限速）
```

完整图形版见 [docs/系统状态机架构图/M1-M8_系统总流程图.html](docs/系统状态机架构图/M1-M8_系统总流程图.html)（浏览器打开）。

## 硬件需求

| 部件 | 说明 |
|:---|:---|
| ESP32-S3 开发板 | 主控 |
| TTL 转 RS485 模块 | DE 方向控制接 GPIO7，详见 [docs/hardware.md](docs/hardware.md) |
| W25Q64 模块 | SPI Flash，CS=9 / SCLK=47 / MOSI=48 / MISO=14（SPI2_HOST） |
| Modbus 从站 | 真实变送器或 PC 端 Modbus Slave 软件模拟 |

## 快速开始

```bash
git clone https://github.com/huangxiongkai/esp32s3-industrial1-gateway.git && cd tcp
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

首次启动自动从 NVS 加载默认配置并连接 WiFi/MQTT（OneNET 物模型属性：
temperature / humidity / current / overflow_count）。

## 目录结构

```
main/
├── app/           业务层：app_task（上报）、state_machine（状态机）、cache_task（断网续传）
├── common/        公共层：types.h、config_manager（NVS 配置）
├── manager/       管理层：device_manager（设备表）
├── scheduler/     调度层：poll_scheduler（Modbus 轮询）
├── protocol/      协议层：mqtt_app、modbus_master、modbus_crc
├── drivers/       驱动层：wifi、rs485、w25q64
└── storage/       存储层：flash_storage（环形缓冲 + 掉电恢复）
```

依赖方向自上而下，禁止反向（详见 ADR004）。

## 文档索引

| 文档 | 内容 |
|:---|:---|
| [docs/PRD.md](docs/PRD.md) | 产品需求定义 |
| [docs/hardware.md](docs/hardware.md) | 硬件接线与外设清单 |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | M1~M8 里程碑变更记录（含踩坑与设计偏差） |
| [docs/ADR/](docs/ADR/) | 架构决策记录 ×7：队列通信、调度分离、RS485 控制、目录结构、Flash 分区、设备建模、采集与网络解耦 |
| [docs/troubleshooting.md](docs/troubleshooting.md) | 9 个高价值故障的完整排查实录（含 Guru Meditation 四步分析法） |
| [docs/系统状态机架构图/](docs/系统状态机架构图/) | 软件架构图、程序流程图、M1-M8 系统总流程图 |

## 里程碑

| 里程碑 | 内容 | 状态 |
|:---|:---|:---|
| M1-M4 | 云端链路 → RTOS 多任务 → 设备管理 → NVS 配置 | 已完成 |
| M5-M7 | 状态机自愈 → RS485 驱动 → Modbus 主站采集闭环 | 已完成 |
| M8 | W25Q64 断网续传（掉电可恢复 + 限速补传） | 已完成 |
| M9 | SPI LCD + LVGL 本地 HMI | 规划中 |
| M10 | OTA 远程升级（双分区回滚） | 规划中 |

## 技术栈

ESP-IDF v5.1.2 · FreeRTOS · esp_mqtt（QoS1）· cJSON · NVS · SPI/UART 驱动 · OneNET 物模型
