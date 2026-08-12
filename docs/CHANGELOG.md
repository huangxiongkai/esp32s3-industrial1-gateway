# 版本变更记录

本文件记录项目所有版本的主要变更，遵循 [Keep a Changelog](https://keepachangelog.com/) 格式。

> **项目时间线基准：2026-06-02（项目初始化）**
> 所有里程碑的预计完成时间从此日期起算。
>
> **记录规则：** 实际实现与架构计划不一致时，在对应里程碑的"设计偏差"一节中记录差异、原因及决策过程。

---

## [M1] - 2026-06-06

### 已完成
- WiFi STA 连接（protocol_examples_common）
- MQTT 连接 OneNET 平台（mqtt_app 组件封装）
- 基础属性上报/下发（温度数据 JSON）

### 耗时
- 5 天（06-02 ~ 06-06）

## [M2] - 2026-06-13

### 已完成
- FreeRTOS 多任务架构：App Task（生产者）+ MQTT Task（消费者）
- Queue 生产者-消费者通信（深度 5，缓冲 10 秒）
- mqtt_app 组件重构：从回调模式改为队列消费模式
- cJSON 动态 JSON 封装（温度模拟递增）
- MQTT 持续上报 OneNET 平台，temperature 实时更新

### 新增文件
- `main/app/app_task.c` — 业务任务（模拟数据 + JSON 封装 + 发队列）
- `main/app/app_task.h` — 业务任务接口
- `main/common/app_msg.h` — Queue 消息结构体定义

### 重构文件
- `components/mqtt_app/mqtt_app.c` — 回调模式 → 队列消费模式
- `components/mqtt_app/include/mqtt_app.h` — 接口从 callback 改为 QueueHandle_t
- `main/app_main.c` — 瘦身为纯初始化 + 任务编排

### 耗时
- 6 天（06-08 ~ 06-13）

## [M3] - 2026-06-20

### 已完成
- Device Manager 设备表管理：静态数组 + Mutex 并发保护
- 模拟采集任务：每 2 秒更新 3 个设备的 temp/humi/current
- App Task 改为从 Device Manager 读取数据，支持多设备 JSON 封装上报
- OneNET 物模型数据上报验证通过

### 新增文件
- `main/manager/device_manager.c` — 设备管理器核心实现
- `main/manager/device_manager.h` — 设备管理器接口
- `main/common/types.h` — 设备数据模型

### 修改文件
- `main/app/app_task.c` — 从 device_manager 读数据 + JSON 封装
- `main/app_main.c` — 调用 device_manager_init
- `main/CMakeLists.txt` — 添加 manager 目录
- `components/mqtt_app/mqtt_app.c` — 添加 payload 日志

### 踩坑记录
- 串口乱码 dev[165]：数组未清零 + for 循环范围错误
- get_all 永远返回 0：return 0 写死
- OneNET 数据不更新：cJSON float 精度太长，平台解析失败

### 耗时
- 6 天（06-15 ~ 06-20）

## [M4] - 2026-06-27

### 已完成
- NVS 配置管理：WiFi/MQTT 参数从 Kconfig 编译时写死改为 NVS Flash 运行时存储
- gateway_config_t 统一配置结构体：wifi_ssid/pass、mqtt_broker/port/user/pass/client_id、轮询/上报周期
- config_manager 四个接口：init（加载）、get（读缓存）、set（写NVS+缓存）、reset_default（恢复出厂）
- mqtt_app 接口改造：从读 Kconfig 宏改为接收外部 gateway_config_t 参数
- 配置持久化验证通过：断电重启后自动从 NVS 加载配置，MQTT 正常连接

### 新增文件
- `main/common/config_manager.c` — NVS 读写实现（blob 整体存取）
- `main/common/config_manager.h` — 配置管理器接口声明

### 修改文件
- `main/common/types.h` — 新增 gateway_config_t 结构体
- `components/mqtt_app/include/mqtt_app.h` — 接口加入 cfg 参数
- `components/mqtt_app/mqtt_app.c` — 内部从 Kconfig 宏改为读 s_cfg 字段
- `main/app_main.c` — 启动流程加入 config_manager_init + get
- `main/CMakeLists.txt` — SRCS 加入 config_manager.c

### 踩坑记录
- ESP_ERR_NVS_NOT_INITIALIZED：nvs_flash_init() 必须在 config_manager_init() 之前调用，否则 NVS 操作全部失败
- mqtt_client: No scheme found：NVS 未初始化导致 mqtt_broker 为空字符串，ESP-IDF 解析 URI 失败

### 耗时
- 6 天（06-22 ~ 06-27）

### 设计偏差（与 M4 架构计划对比）
| 条目 | 架构计划方案 | 实际实现方案 | 原因 |
|:---|:---|:---|:---|
| `mqtt_app_stop()` 注销方式 | 架构计划中未详细定义，默认采用 `vTaskDelete` 强删 | 退出消息队列方式：发送 `topic[0]=='\0'` 的特殊消息唤醒 publish 任务自行退出，通过 `eTaskGetState` 轮询等待确认 | 退出消息方式延迟更低（无需猜测延时）、不会在 client destroy 时竞态，同时遵循"任务自行退出而非强删"的 RTOS 最佳实践 |

---

## [M5] - 2026-07-06

### 已完成
- 系统状态机（4 状态：INIT → WIFI_CONN → MQTT_CONN → RUNNING）
- WiFi 驱动封装（wifi_driver 独立模块，事件通知机制）
- 状态机自愈：WiFi 断连回退到 WIFI_CONN，MQTT 断连回退到 MQTT_CONN
- 禁用 esp_mqtt 自动重连（session.disable_auto_reconnect = true），统一由状态机管控
- mqtt_app 生命周期管理：start/stop 支持重入，stop 时销毁 client + 删除 publish task
- app_task 生命周期管理：volatile bool 退出标志 + vTaskDelete(NULL) 安全退出
- 事件驱动架构：状态机通过事件队列接收 WiFi/MQTT 断连通知
- 无限重试机制：WiFi 5s 间隔，MQTT 3s 间隔，工业场景不能放弃

### 新增文件
- `main/app/state_machine.c` — 系统状态机核心实现
- `main/app/state_machine.h` — 系统状态机接口
- `main/drivers/wifi.c` — WiFi STA 驱动封装
- `main/drivers/wifi.h` — WiFi 驱动接口

### 修改文件
- `main/app_main.c` — 瘦身为纯初始化 + state_machine_run
- `main/app/app_task.c` — 添加 start/stop 生命周期管理
- `main/app/app_task.h` — 添加 start/stop 接口
- `components/mqtt_app/mqtt_app.c` — 添加 start/stop 生命周期，禁用自动重连
- `components/mqtt_app/include/mqtt_app.h` — 添加 start/stop 接口
- `main/CMakeLists.txt` — 添加 drivers 目录
- `main/common/types.h` — 新增 sys_event_t 事件枚举

### 踩坑记录
- FreeRTOS Task should not return：app_task 循环退出后隐式 return，触发 abort。修复：循环结束后加 vTaskDelete(NULL)
- esp_mqtt_client_destroy 触发事件：调用 destroy 时会触发 MQTT_EVENT_DISCONNECTED，需先将 s_sys_event_queue = NULL 阻断通知
- 状态机队列残留事件：wifi_driver_stop() 会触发 DISCONNECTED 事件，stop→start 期间需 while(xQueueReceive) 清空

### 耗时
- 8 天（06-29 ~ 07-06）

### 设计偏差（与 M5 架构计划对比）
| 条目 | 架构计划方案 | 实际实现方案 | 原因 |
|:---|:---|:---|:---|
| mqtt_app 停止方式 | 架构计划中未详细定义 | 发送特殊消息（topic[0]=='\0'）唤醒 publish task 自行退出 | 遵循"任务自行退出而非强删"的 RTOS 最佳实践，避免竞态 |
| 状态机事件通知 | 架构计划使用事件组（EventGroup） | 实际使用事件队列（Queue） | 队列可以携带事件数据，更灵活；且与 app_msg.h 的消息结构统一 |

---

## [M6] - 2026-07-11

### 已完成
- RS485 驱动封装：UART2 配置（9600-8N1）+ DE 引脚方向控制（GPIO7）
- 半双工收发流程：拉高 DE → uart_write_bytes → uart_wait_tx_done → 拉低 DE
- 阻塞接收 + 超时机制：uart_read_bytes + pdMS_TO_TICKS
- 接收缓冲区清空：uart_flush_input 防止残留数据
- 状态机 INIT 阶段集成 rs485_init()
- 裸字节验证通过：PC 端 USB 转 RS485 + SSCOM 正确收到 01 03 00 00 00 01 84 0A

### 新增文件
- `main/drivers/rs485.c` — RS485 驱动实现（init/send/receive/flush）
- `main/drivers/rs485.h` — RS485 驱动接口声明 + 引脚/参数宏定义

### 修改文件
- `main/app/state_machine.c` — handle_init() 中加入 rs485_init() 调用
- `main/CMakeLists.txt` — REQUIRES 添加 driver 组件

### 踩坑记录
- `uart_read_bytes` 超时参数：最后一个参数单位是 Tick 不是毫秒，必须用 pdMS_TO_TICKS 转换
- TX 缓冲区设为 0：半双工同步发送不需要 TX 缓冲，uart_driver_install 的 tx_buffer_size 传 0

### 耗时
- 4 天（07-08 ~ 07-11）

---

## [M7] - 2026-07-22

### 已完成
- Modbus CRC16 校验：逐位计算法（多项式 0xA001，初始值 0xFFFF，LSB-first）
- Modbus RTU 主站：0x03 读保持寄存器（组帧→flush→发送→接收→CRC校验→异常帧检查→解析）
- 寄存器映射表（表驱动）：modbus_reg_map_t 显式声明从站地址/寄存器/缩放因子/目标字段
- poll_scheduler 独立轮询任务：周期遍历设备表 → modbus_read_device → device_manager_update
- 设备离线检测（双机制）：连续 3 次失败计数（主动）+ 时间戳超时兜底（被动）
- RS485 方向切换毛刺容错：跳过前导 0x00 垃圾字节
- 状态机集成：RUNNING 状态启停 poll_scheduler，回退时先停采集再停上报
- PC Modbus Slave 联调验证通过：3 设备（2 在线 + 1 离线），云端数据实时更新

### 新增文件
- `main/protocol/modbus_crc.c` — CRC16 校验实现
- `main/protocol/modbus_crc.h` — CRC16 接口
- `main/protocol/modbus_master.c` — Modbus RTU 主站（发送/接收/解析/高层封装）
- `main/protocol/modbus_master.h` — 主站接口 + 寄存器映射表结构体
- `main/scheduler/poll_scheduler.c` — 轮询调度任务（设备表遍历 + 离线检测）
- `main/scheduler/poll_scheduler.h` — 调度器接口

### 修改文件
- `main/manager/device_manager.c` — 关闭模拟采集任务 + 实现 check_offline 时间戳兜底
- `main/app/state_machine.c` — RUNNING 状态集成 poll_scheduler 启停
- `main/CMakeLists.txt` — 注册 modbus_crc/modbus_master/poll_scheduler

### 踩坑记录
- RS485 方向切换毛刺：DE 1→0 瞬间总线浮空产生前导 0x00，CRC 永远失败。修复：memmove 跳过前导 0x00
- 设备 ID 不一致：poll_scheduler 用 dev_id=0/1/2，device_manager 用 id=1/2/3，update 找不到槽位。修复：统一为 1/2/3
- 字段映射靠猜：用 map_count 推断设备类型导致温湿度被填进电流字段。修复：显式 field 枚举映射

### 设计决策
| 决策 | 选择 | 理由 |
|:---|:---|:---|
| CRC 算法 | 逐位法（非查表法） | 代码简洁、面试好讲、M7 性能够用 |
| 离线判定 | 连续 3 次失败 + 时间戳兜底 | 迟滞设计防抖动，双机制互补 |
| 寄存器映射 | 一条表项=一个寄存器=一个字段 | 显式无歧义，加设备只改表不改逻辑 |
| MQTT 断连时 | 停止采集（M7 简化） | M8 加 Flash 缓存后改为"断网不停采、存 Flash" |

### 耗时
- 10 天（07-13 ~ 07-22）

---

## [M8] - 2026-08-11

### 已完成
- W25Q64 SPI Flash 驱动：页编程/扇区擦除/BUSY 轮询/读写封装
- flash_storage 存储层：256B 条目=1 页、三态 magic（VALID/SENT/BLANK）、
  上电全扫描重建读写指针、满溢按扇区覆盖 + overflow 计数 NVS 持久化、
  at-least-once（read 与 mark_sent 分离）、损坏条目自愈跳过
- cache_task 转发枢纽（store-and-forward）：队列消费、在线/离线分流、
  发布失败兜底落盘、上线上升沿 overflow 上报（take-and-clear + 失败归还）、
  限速补传（时间戳比较实现 1 条/秒共享配额，不用 vTaskDelay）
- mqtt_app 退化为纯发布工具：删 publish task，新增同步 mqtt_app_publish
- app_task 合并上报：每设备一条 → 每轮一条，OneNET 限流合规（1.5→0.5 条/秒）
- 采集与网络解耦（ADR007）：采集任务 INIT 启动一次永不停止，
  状态机仅用 set_online 切换路由，断网期间照常产数据写 Flash
- cJSON 定精度格式化（snprintf + AddRawToObject），解决平台步长校验拒收
- esp_mqtt 网络超时 10s→3s，消除静默断连过渡窗口丢数据
- post/reply 平台应答观测日志（云端拒收的唯一诊断窗口）
- 联调全矩阵通过：直通/断网缓存/恢复补传/补传中再断网/满溢/补传中断电

### 新增文件
- `main/drivers/w25q64.c/h` — SPI Flash 驱动
- `main/storage/flash_storage.c/h` — 环形缓冲存储层
- `main/app/cache_task.c/h` — 离线缓存转发任务

### 修改文件
- `main/protocol/mqtt_app.c/h` — 发布工具化、network.timeout_ms=3000、post/reply 观测
- `main/app/app_task.c` — 合并上报 + 定精度格式化
- `main/app/state_machine.c` — ADR007 接线（INIT 一次性启动、set_online 进出 RUNNING）
- `main/app_main.c` — flash_storage_init
- `main/CMakeLists.txt` — 注册 w25q64/flash_storage/cache_task

### 踩坑记录
- cJSON double free：半截重构（create 搬出循环、delete 没搬），Guru Meditation 四步法定位（详见 troubleshooting 问题 8）
- 静默断连发布阻塞 10s 队列溢出丢 3 条：日志迟到 10 秒定位任务阻塞（问题 9）
- flash_storage_init 漏调用 → NULL 互斥锁 assert 崩溃循环
- 全 SENT 扫描读指针错置 → 补传在旧条目上白烧计数，新数据完好却无人读
- OneNET 拒收三连：2306 标识符不存在 / 2254 浮点步长校验（0.51999998 病根）/ 2405 id 非数字

### 设计决策
| 决策 | 选择 | 理由 |
|:---|:---|:---|
| 满溢策略 | 按扇区覆盖最旧 16 条 + 计数上报 | 擦除粒度对齐，丢多少云端可见 |
| 重传语义 | at-least-once | 遥测宁重勿丢，exactly-once 成本过高 |
| 时间戳 | 暂填 0 保留字段 | 无 NTP，云端以接收时间为准 |
| 物模型标识符 | 裸 key + 功能名称区分 | 无字段冲突，后缀方案修订废止（见 ADR006 修订） |

### 耗时
- 19 天（07-24 ~ 08-11）

---

## [Unreleased]

### 计划添加（路线 A）
- M9: SPI LCD + LVGL HMI — 工业监控界面
- M10: OTA 远程升级 — 双分区回滚
