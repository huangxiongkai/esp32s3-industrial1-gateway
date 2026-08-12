# 硬件配置

## 板卡信息

| 项目 | 值 |
|:---|:---|
| 板卡型号 | 正点原子 DNESP32-S3 |
| SoC | ESP32-S3 |
| CPU | 双核 Xtensa LX7，240 MHz |
| 内部 SRAM | 512 KB |
| 静态 IRAM | 16 KB（始终 100% 使用，正常现象） |
| Flash | 16 MB |
| PSRAM | 查阅板卡手册确认 |

## sdkconfig 设置

| 配置项 | 值 | 备注 |
|:---|:---|:---|
| CONFIG_IDF_TARGET | esp32s3 | |
| CONFIG_ESPTOOLPY_FLASHSIZE | 16MB | 于 2026-07-25 从 2MB 修改 |
| CONFIG_SPIRAM | 未设置 | 除非确认存在 PSRAM，否则不要启用 |

## 注意事项

- 静态 IRAM 占用 100% 对 ESP32-S3 而言是正常现象，并非问题。
- 主 SRAM 使用情况显示在 "Used stat D/IRAM" 行。
- Flash 大小会影响分区表和 OTA 功能。


## 硬件接线图
| MAX3485 模块引脚 | 接 ESP32-S3 开发板 | 功能说明 |
| :---            | :---               | :--- |
| **VCC**         | 3.3V               | 模块供电（MAX3485 支持 3.3V） |
| **GND**         | GND                | 系统共地，必须连接 |
| **DI**          | GPIO17 (UART2 TX)  | MCU 发送数据至 485 总线 |
| **RO**          | GPIO18 (UART2 RX)  | MCU 接收 485 总线数据 |
| **DE**          | **GPIO7**          | **短接后接同一 GPIO**。1=发送模式，0=接收模式 |
| **/RE**         | **GPIO7**          | 与 DE 短接 |
| **A**           | 总线 A 线           | RS485 差分信号正端 |
| **B**           | 总线 B 线           | RS485 差分信号负端 |

## W25Q64 SPI Flash 模块（M8）

| 项目 | 值 |
|:---|:---|
| 芯片 | W25Q64（8MB / 64Mbit） |
| 接口 | SPI（Mode 0/3，最高 104MHz） |
| 用途 | 断网续传数据缓存 + 系统事件日志 |

| W25Q64 引脚 | 接 ESP32-S3 | 功能说明 |
| :--- | :--- | :--- |
| **VCC** | 3.3V | 供电 |
| **GND** | GND | 地 |
| **CS** | GPIO9 | 片选（低有效） |
| **CLK** | GPIO47 | SPI 时钟（SPI3_HOST） |
| **MOSI (DI)** | GPIO48 | 主出从入 |
| **MISO (DO)** | GPIO14 | 主入从出 |

> 使用 SPI3_HOST 控制器，通过 GPIO matrix 映射（非 IO_MUX 默认引脚），ESP32-S3 允许 SPI 信号映射到任意普通 GPIO。SPI2_HOST 特意保留给 M9 的 SPILCD / TF 卡，避免 W25Q64 的轮询事务与 LCD 的 DMA 刷新争抢同一总线导致屏闪。
>
> **引脚选择依据（依据正点原子 DNESP32S3 硬件参考手册 表 1.2.2.1，避开五类引脚）：**
> - 避开 strapping pin：GPIO0 / 3 / 45 / 46（影响启动电平，GPIO45 决定 Flash 供电电压）
> - 避开 Octal PSRAM 占用：GPIO35 / 36 / 37（手册明确标注"勿用"，模组 8MB PSRAM 内部占用）
> - 避开 USB：GPIO19 / 20（USB D-/D+，留作下载调试）
> - 避开板上 SPILCD / TF 卡共用网络：GPIO2 / 11 / 12 / 13 / 40（手册注明"TF 卡接口和 SPILCD 接口共用一个 SPI 接口"，留给 M9 LCD）
> - 避开触摸 IC：GPIO38 / 39（板上 CT_SCL / CT_SDA，留给 M9 触摸）
>
> 所选 4 个引脚（9/47/48/14）均挂在 RGB LCD / 摄像头 / I2S 音频网络上，本项目不用这些外设时即为空闲。
>
> **已占用引脚清单：** GPIO7（RS485 DE）、GPIO17（UART2 TX）、GPIO18（UART2 RX）、GPIO9/47/48/14（W25Q64 SPI）。
>
> 若实际接线发现冲突，可更换为其他普通 GPIO，只需修改 `drivers/w25q64.h` 中的引脚宏。

## SPI LCD 触摸屏（M9）

| 项目 | 值 |
|:---|:---|
| 接口 | SPI + 触摸（I2C 或 SPI） |
| 用途 | 本地 HMI 工业监控界面（LVGL） |

> 具体型号、分辨率、GPIO 分配在 M9 开发时根据实际模块确定。

