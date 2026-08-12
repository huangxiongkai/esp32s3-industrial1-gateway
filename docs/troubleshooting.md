# 常见问题与解决办法

## MQTT 问题排查清单

1. 核查 broker 地址、端口以及 WiFi 连接是否正常
2. 核查 id 等各信息是否正确
3. 数据能否正常生成
4. 数据是否正确上报/发出（topic 是否选对，使用的 topic 是否有权限）
5. 上报格式是否正确
6. 平台是否收得到/解析得到
7. 平台下发命令后，设备端是否有响应？订阅的 QoS 是否匹配？

重点工具：ESP-IDF 串口终端输出 + MQTT 调试日志

---

## 问题 1：MQTT.fx 连接 OneNET 失败

### 现象
MQTT.fx 尝试连接 OneNET Broker 时被拒绝，日志显示：
```
attempt to connect with MQTT v5.0
```

### 原因
MQTT.fx 默认使用 MQTT v5.0 协议，但 OneNET 只支持 MQTT v3.1.1。协议版本不匹配，Broker 直接拒绝连接。

### 解决办法
1. 打开 MQTT.fx 连接设置
2. 找到 **MQTT Version** 选项（在 General 或 TLS/SSL 旁边）
3. 从 `v5.0` 改成 `v3.1.1`
4. 重新连接

### 备注
- ESP32 的 `esp_mqtt` 组件默认使用 v3.1.1，所以能正常连接
- 如果 MQTT.fx 界面找不到改版本的地方（部分版本锁死 v5.0），建议换用 MQTTX（默认 v3.1.1）

---

## 问题 2：OneNET 上报数据失败，返回错误码 2302

### 现象
MQTTX 发送数据后收到回复：
```json
{"id":"123","code":2302,"msg":"function mode error:identifier:temperature"}
```
设备属性在网页上一直显示 `undefined`。

### 原因
物模型定义的是**属性（property）**，但使用了**事件（event）**的 Topic。OneNET 对属性和事件使用不同的 Topic 路径。

### 解决办法
确认物模型的 `functionMode`，选择对应的 Topic：

**OneNET 物模型 MQTT Topic 完整列表：**

| 功能 | 方向 | Topic 格式 | 用途 |
|------|------|-----------|------|
| 属性上报 | 设备→平台 | `$sys/{pid}/{device}/thing/property/post` | 上报传感器数据 |
| 属性回复 | 平台→设备 | `$sys/{pid}/{device}/thing/property/post/reply` | 平台确认收到 |
| 属性设置 | 平台→设备 | `$sys/{pid}/{device}/thing/property/set` | 平台下发控制指令 |
| 属性设置回复 | 设备→平台 | `$sys/{pid}/{device}/thing/property/set/reply` | 设备确认收到 |
| 事件上报 | 设备→平台 | `$sys/{pid}/{device}/thing/event/post` | 上报设备事件 |
| 事件回复 | 平台→设备 | `$sys/{pid}/{device}/thing/event/post/reply` | 平台确认收到 |
| 服务调用 | 平台→设备 | `$sys/{pid}/{device}/thing/service/call` | 平台调用设备服务 |
| 服务回复 | 设备→平台 | `$sys/{pid}/{device}/thing/service/call/reply` | 设备回复服务结果 |
| 属性获取 | 设备→平台 | `$sys/{pid}/{device}/thing/property/get` | 设备主动查询属性 |
| 属性获取回复 | 平台→设备 | `$sys/{pid}/{device}/thing/property/get/reply` | 平台返回属性值 |

其中 `{pid}` = 产品ID，`{device}` = 设备名称

**常见 Topic 组合示例：**

```
# 属性上报（最常用）
发布：$sys/FF1vl83pTz/mqttx_f8135be9/thing/property/post
订阅：$sys/FF1vl83pTz/mqttx_f8135be9/thing/property/post/reply

# 事件上报
发布：$sys/FF1vl83pTz/mqttx_f8135be9/thing/event/post
订阅：$sys/FF1vl83pTz/mqttx_f8135be9/thing/event/post/reply

# 接收平台控制指令
订阅：$sys/FF1vl83pTz/mqttx_f8135be9/thing/property/set
发布：$sys/FF1vl83pTz/mqttx_f8135be9/thing/property/set/reply
```

**Payload 格式：**

属性上报：
```json
{
  "id": "123",
  "version": "1.0",
  "params": {
    "temperature": {
      "value": 39
    }
  }
}
```

事件上报：
```json
{
  "id": "123",
  "version": "1.0",
  "events": {
    "alarm_event": {
      "value": {
        "temperature": 39,
        "level": "warning"
      }
    }
  }
}
```

属性回复（平台返回）：
```json
{"id": "123", "code": 200, "msg": "success"}
```

### 备注
- `id` 是消息编号，随便填，用于匹配请求和回复
- 订阅和发布的 Topic 必须配套（如 property/post 对应 property/post/reply）
- 属性上报用 `property/post`，事件上报用 `event/post`，不要混用

### Topic 操作权限（ACL）

OneNET 对每个 Topic 预设了操作权限，**设备只能执行允许的操作**，否则报 `Not authorized (Code: 128)`：

| Topic | 权限 | 说明 |
|-------|------|------|
| `.../thing/property/post` | **发布** | 设备只能 publish，不能 subscribe |
| `.../thing/property/post/reply` | **订阅** | 设备只能 subscribe，不能 publish |
| `.../thing/property/set` | **订阅** | 设备只能 subscribe，接收平台下发指令 |
| `.../thing/property/set_reply` | **发布** | 设备只能 publish，回复平台指令 |

**常见错误：** 用 MQTTX 订阅 `property/post` 会报 Not authorized，因为该 topic 只允许发布。正确做法是订阅 `property/post/reply`，或者直接在 OneNET 控制台查看数据。

---

## 问题 3：串口输出乱码，设备 ID 变成 165，name 显示 MQTT topic

### 现象

串口日志中，前 3 个设备数据正常：
```
APP_TASK: Sent: dev[1] sensor_01: T=25.2 H=51.2 I=1.18
APP_TASK: Sent: dev[2] sensor_02: T=27.8 H=45.8 I=0.70
APP_TASK: Sent: dev[3] sensor_03: T=22.3 H=61.6 I=1.53
```

但紧接着出现 5 条异常数据：
```
APP_TASK: Sent: dev[165] ����...$sys/FF1vl83pTz/ESP32_02/thing/property/post: T=-0.0 H=-0.0 I=-0.00
```

特征：设备 ID 变成 165（0xA5），name 字段显示 MQTT topic 字符串，温度湿度电流全为 -0.0。

每轮循环都是 3 正常 + 5 垃圾，固定 8 条。

### 原因分析

**第一步：排除栈溢出**

最初怀疑是 `app_task` 栈溢出（4096 字节），将栈增大到 8192 后问题依旧，排除栈溢出。

**第二步：定位到 for 循环范围**

每轮固定输出 8 条（3 正常 + 5 垃圾），而 `app_task` 的 for 循环写的是：
```c
for(int i = 0; i < MAX_DEVICES; i++)  // MAX_DEVICES = 8
```
循环固定遍历 8 次，但 `device_manager_get_all` 实际只返回 3 个有效设备。

**第三步：定位到数组未清零**

`devices` 是 `app_task` 的局部变量（栈上），每次 while 循环不会自动清零。
`get_all` 只填充 `devices[0..2]`，`devices[3..7]` 保留栈上的残留垃圾值。

**第四步：解释乱码内容**

- `dev[165]`：165 = 0xA5，是 `devices[3].id` 内存位置上的栈残留值
- name 显示 MQTT topic：`msg.topic` 和 `devices[3].name` 在栈上相邻，topic 字符串踩进了 name 的内存位置
- `T=-0.0`：float 全零的二进制表示，是未初始化内存的典型特征

### 根本原因

两个问题叠加：
1. `devices` 数组在每轮循环开始时没有清零，残留上一轮的垃圾数据
2. for 循环用 `MAX_DEVICES`（8）而不是 `count`（3），导致垃圾数据也被处理

### 解决办法

app_task.c 两处修改：

```c
static void app_task(void *pvParameters)
{
    device_data_t devices[MAX_DEVICES];
    while (1)
    {
        // 修复 1：清零数组，防止残留垃圾
        memset(devices, 0, sizeof(devices));

        int count = device_manager_get_all(devices, MAX_DEVICES);

        // 修复 2：用 count 而不是 MAX_DEVICES
        for(int i = 0; i < count; i++)
        {
            // ... JSON 封装和发送逻辑不变 ...
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

### 经验教训

1. **局部数组不会自动清零**：C 语言栈上的局部变量初始值是随机的，必须手动 memset
2. **循环范围要和实际数据量匹配**：get_all 返回多少就遍历多少，不要用固定的最大值
3. **排查问题时先排除再定位**：先增大栈排除栈溢出，再从日志规律（3+5=8）反推循环范围问题

---

## 问题 4：OneNET 物模型数据不更新，串口显示 Published 但平台无变化

### 现象

串口日志显示 MQTT publish 成功：
```
MQTT: Published: topic=$sys/FF1vl83pTz/ESP32_02/thing/property/post
payload={"id":"1","version":"1.0","params":{"temperature":{"value":24.819999694824219}}}
```

但 OneNET 控制台上 temperature 值不更新，始终显示旧值。

### 原因

OneNET 物模型定义 temperature 为 **int32** 类型，但 cJSON 生成的 float 值带有超长小数精度（如 `24.819999694824219`），平台解析失败，静默丢弃数据。

### 解决办法

app_task.c 中对 float 值强转为 int：

```c
// 修改前（错误）
cJSON_AddNumberToObject(temp_obj, "value", devices[i].temp);

// 修改后（正确）
cJSON_AddNumberToObject(temp_obj, "value", (int)devices[i].temp);
```

### 经验教训

1. **物模型类型要对齐**：OneNET 定义 int32 就发整数，定义 float 才发小数
2. **cJSON 的 float 精度问题**：`cJSON_AddNumberToObject` 对 float 会保留完整精度，产生超长小数，平台可能无法解析
3. **Published 不等于成功**：`esp_mqtt_client_publish` 返回的是 msg_id，不代表平台已收到并解析成功

---

## 问题 5：WiFi 断连后设备崩溃重启（FreeRTOS Task should not return）

### 现象

设备处于 RUNNING 状态，关闭路由器后，状态机正确捕获断连并尝试回退：
```
W (1007645) WIFI_DRV: WiFi disconnected
W (1007655) STATE_MACHINE: WiFi lost in RUNNING, falling back to WIFI_CONN
```
但随后设备直接崩溃重启：
```
E (1009405) FreeRTOS: FreeRTOS Task "app_task" should not return, Aborting now!
abort() was called at PC 0x4037ea4f on core 1
```

### 根因

`app_task` 函数在 `while (s_app_task_running)` 循环退出后，没有调用 `vTaskDelete(NULL)`，而是走到了函数末尾的 `}` 隐式 return。

FreeRTOS 的任务函数**永远不能 return**。任务创建时分配的 TCB 和栈空间只能通过 `vTaskDelete` 回收。隐式 return 导致栈指针回到不存在的调用者，调度器检测到异常后触发 abort 保护。

### 排查路径

1. **读崩溃日志**：`FreeRTOS Task "app_task" should not return` 直接指明了崩溃任务是 `app_task`，原因是"不应该返回"
2. **对比正确实现**：`mqtt_publish_task` 在循环退出后调用了 `vTaskDelete(NULL)`，正常工作；`app_task` 循环退出后什么都没有，直接 return
3. **确认触发路径**：状态机调 `app_task_stop()` → 设 `s_app_task_running = false` → while 循环退出 → 隐式 return → abort

### 修复方案

`main/app/app_task.c`，在 while 循环结束后加一行：

```c
static void app_task(void *pvParameters)
{
    device_data_t devices[MAX_DEVICES];
    while (s_app_task_running)
    {
        // ... 业务逻辑 ...
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    vTaskDelete(NULL);   // 安全删除自身，不能 return
}
```

### 防御机制

1. **编码规范**：所有 FreeRTOS 任务函数必须以 `vTaskDelete(NULL)` 结尾，禁止隐式 return
2. **Code Review 检查项**：新增任务时，检查函数是否有"退出路径"，如果有，退出路径末尾必须有 `vTaskDelete(NULL)`
3. **对比模板**：新写任务函数时，参照 `mqtt_publish_task` 的结构（while + break + vTaskDelete）

### 经验教训

1. **FreeRTOS 任务 ≠ 普通函数**：普通函数可以 return，任务函数不行。这是 RTOS 新手最常踩的坑之一
2. **崩溃日志是最好的线索**：`should not return` 六个字直接定位了问题，不需要看栈回溯
3. **stop 机制要配套**：设计了 `volatile bool` 退出标志，就必须配套 `vTaskDelete(NULL)`，两者缺一不可

---

## 问题 6：Modbus 响应帧头稳定多出 0x00，CRC 校验永远失败

### 现象

ESP32 通过 RS485 向 PC Modbus Slave 发送 0x03 请求，收到响应但 CRC 校验永远失败：
```
W MODBUS: CRC error, len=10    ← 设备1，理论应为 9 字节
W MODBUS: CRC error, len=8     ← 设备2，理论应为 7 字节
W MODBUS: CRC error, len=1     ← 设备3（无从站），理论应为 0（超时）
```

特征：每个响应都恰好多 1 字节（+1 规律），且设备 3 无从站却收到 1 字节。

### 根因

**RS485 半双工方向切换毛刺。**

DE 引脚从 1→0（发送切接收）瞬间，总线短暂浮空被拉低。ESP32 的 UART RX 将这个"假下降沿"误判为起始位，采样 8 个数据位全为 0，生成一个 `0x00` 垃圾字节，排在真实响应帧前面。

```
UART 帧结构：[起始位=0][D0~D7][停止位=1]
毛刺产生过程：总线浮空拉低 → 假起始位 → 8bit全采0 → 结果 = 0x00
```

为什么是 0x00 而不是其他值：起始位（低电平）+ 总线未稳定时 8 个数据位全采成 0 = 0x00。这是方向切换毛刺的典型特征。

### 排查路径

1. **观察 len 规律**：三个设备都 +1，且无从站的设备 3 也收到 1 字节 → 不是随机噪声，是系统性前导字节
2. **加 hex 打印**：`ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, n, ESP_LOG_WARN)` 确认首字节为 0x00
3. **排除接线问题**：杜邦线松会加剧毛刺，但随机噪声不会 75 次都稳定 +1 → 主因是方向切换，接线是帮凶
4. **确认安全性**：合法 Modbus 响应首字节是从站地址（1~247），永不为 0x00 → 跳过前导 0x00 不会误删有效数据

### 修复方案

`modbus_master.c` 的 `modbus_recv_response()` 中，CRC 校验前跳过前导 0x00：

```c
// RS485 半双工方向切换会产生前导 0x00 毛刺字节
int offset = 0;
while (offset < n && buf[offset] == 0x00) {
    offset++;
}
if (offset > 0) {
    memmove(buf, buf + offset, n - offset);
    n -= offset;
}
```

### 防御机制

1. **硬件层面**：RS485 总线加偏置电阻（A 上拉至 VCC、B 下拉至 GND），让空闲态稳定在 mark（高电平），消除浮空毛刺
2. **软件层面**：接收后跳过前导 0x00 作为容错（已实现）
3. **接线规范**：工业现场使用屏蔽双绞线 + 端子压接，不用杜邦线

### 经验教训

1. **RS485 半双工的方向切换是 bug 高发区**：DE 引脚时序、总线偏置、收发器使能顺序，任何一个不对都会产生假字节
2. **分析日志先看规律再定位**：+1 的规律性直接排除了"随机干扰"，指向系统性原因
3. **0x00 是 UART 毛刺的"指纹"**：看到稳定前导 0x00，第一反应应该是方向切换/总线浮空，而不是波特率或接线

---

## 问题 7：Modbus 从站全部读取超时（ERR_TIMEOUT）——一次完整的分层硬件排查

### 现象

M8 集成 Modbus 采集时，三个从站设备全部读取超时，数据采不上来：
```
POLL_SCHED: Device 1 [sensor_th] fail (1)(3) , err = 1
POLL_SCHED: Device 2 [sensor_cur] fail (2)(3) , err = 1
POLL_SCHED: Device 3 [sensor_bak] fail (3)(3) , err = 1
...
POLL_SCHED: Device 1 [sensor_th] OFFLINE
```
错误码 1 = `ERR_TIMEOUT`：主站发了请求，但收不到任何从站响应。

### 排查路径（分层递进，不跳层）

**第 0 步：git diff 排除代码因素**

先确认 Modbus 协议栈相对 M7 稳定版一行逻辑都没改（`modbus_master.c`、`modbus_crc.c`、`rs485.c` 均未动），把排查范围锁定在**物理层和接线**，避免在代码上浪费时间。

**第 1 步：逻辑分析仪确认主站发送正常**

探头接 ESP32 TX（GPIO17），抓到周期性请求帧，解码为正确的 Modbus RTU 报文（`01 03 00 00 00 01 ...`），位时序 104.167µs 反推出波特率 9600 与代码一致。**结论：软件发送没问题，故障在接收端。**

**第 2 步：探测接收线，踩了仪器接线的坑**

直接把逻辑分析仪接 GPIO18 会干扰启动（板子无法烧录）。改用**面包板一分二**：GPIO18、模块 RXD、逻辑分析仪通道插同一排并联探测。

**第 3 步：发现 RS485 模块双 GND 陷阱**

模块左右两侧各有一个 GND，且**内部不连通**（TTL 侧地与 RS485 侧地分离）。最初只接了 A/B 侧 GND，导致 TTL 侧没有共地参考，收不到数据。补接 TXD 侧 GND 后，总线上出现了 8 帧波形。

**第 4 步：险些被"8 帧"表象误导（关键教训）**

看到 8 帧时一度以为通信恢复，推断"8 帧 = 4 请求 + 4 响应 = 全双工通了"。**但当时没有做协议解码**。事后复盘：那时 RX 接法仍是错的，那 8 帧其实只是主站在**两个轮询周期里发的请求帧**（每周期 4 个请求 × 2 周期 = 8），从站根本没响应。

**第 5 步：定位真正根因——TXD/RXD 标注视角**

模块丝印的 TXD/RXD 存在两种标注视角：有的按"模块自身收发"标注，有的按"对端收发"标注。本模块是对端视角，**ESP32 的 TX（GPIO17）应接模块标着 RXD 的脚**，而按直觉接了 TXD 导致不通。改正后通信立即恢复。

### 最终验证（铁证）

不看帧数，看**数据内容是否对得上**：
```
APP_TASK: dev[1] sensor_th: T=25.0 H=50.0
```
温度 25.0、湿度 50.0 与 Modbus Slave 里填的寄存器值（250×0.1、500×0.1）完全吻合——这才是数据真正走通的证据。

### 经验教训

1. **仪器上看到"有波形"≠"通信正常"**：必须解码到正确的协议内容，并且收发两端都验证。只数帧数不做解码，会被主站自己的发送帧误导。
2. **硬件排查要分层收敛**：物理层（电平/接线）→ 驱动层（UART 配置）→ 协议层（帧/CRC）→ 应用层（设备表），每层用对应工具验证，不在错误方向浪费时间。
3. **RS485 模块的双 GND 和 TXD/RXD 丝印都是"认知盲区"**：拿到新模块先看手册或实测，不依赖丝印直觉。
4. **数据比对是最终裁判**：读到的物理量与预期值精确吻合，比任何"看起来正常"的波形都有说服力。

---

## 问题 8：app_task 合并上报改造后不定期复位（cJSON double free / use-after-free）

### 现象

M8 Step 4 把 app_task 从"每设备一条 JSON"改造为"合并上报"后，设备不定期崩溃复位，且**每次崩溃点不一样**：

崩溃一（堆断言）：
```
I (8592) APP_TASK: [MQTT_TX] dev[1] sensor_th: T=25.0 H=50.0 I=0.00
assert failed: heap_caps_free heap_caps.c:387
(heap != NULL && "free() target pointer is outside heap areas")
Backtrace: ... free → cJSON_Delete → app_task at app_task.c:69
```

崩溃二（非法读）：
```
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
EXCVADDR: 0x00000005
Backtrace: ... add_item_to_array → cJSON_AddObjectToObject → app_task at app_task.c:62
```

关键观察：**设备 1 每次都正常打印，设备 2 还没打印就崩**——第 1 轮迭代安全，第 2 轮迭代出事。

### 根因：半截重构——create 搬出了循环，delete 没跟着搬

改造时把 `cJSON_CreateObject()` 移到 for 循环外（合并的前提），但"序列化 + `cJSON_Delete` + 入队"仍留在循环体内：

- root **创建 1 次**（循环外），**删除 N 次**（循环内每轮一次）
- 第 1 轮 `cJSON_Delete(root)` 释放 root 及其子节点 params，**但指针没置 NULL**
- 第 2 轮两种死法，取决于哪种动作先发生：

| 死法 | 机制 | 对应崩溃 |
|:---|:---|:---|
| **Use-after-free** | 第 2 轮往已释放的 params 里挂新节点，内存已被堆复用，内部链表指针是垃圾值 → 读到 0x5 | 崩溃二（62 行 LoadProhibited） |
| **Double free** | 第 2 轮再次 `cJSON_Delete(root)`，堆管理器发现该地址不在活动块管理范围 | 崩溃一（69 行 free 断言） |

**崩溃点漂移**（两次崩在不同行）是堆破坏类 bug 的典型特征：雷是同一个，踩的角度不同。

### 排查路径（通用 Guru Meditation 四步法，本 case 实录）

崩溃报告分 4 个区，排查顺序是**区①定性质 → 区③定位置 → 区②定细节**，不是从上往下全读：

**第 1 步：看异常类型（括号里的词）——知道"出了什么事"**

| 异常类型 | 含义 | 最常见原因 |
|:---|:---|:---|
| LoadProhibited（0x1C） | 读了不该读的地址 | 空指针、野指针、已释放对象 |
| StoreProhibited（0x1D） | 写了不该写的地址 | 同上，后果更严重 |
| IllegalInstruction（0x00） | 执行了非法指令 | 函数指针被踩、栈破坏 |
| IntegerDivideByZero（0x06） | 除零 | 分母没判空 |

**第 2 步：看 Backtrace——定位到自己的代码行**

从上往下是"从内到外"，找**第一个属于自己文件的帧**。本 case：`app_task at app_task.c:69`（崩溃一）/`:62`（崩溃二）。
> 函数名是 `idf.py monitor` 用 ELF 实时解码的；用其他串口工具只有十六进制时，手动跑 `xtensa-esp32s3-elf-addr2line -pfiaC -e build/xxx.elf 0x地址`。

**第 3 步：看 EXCVADDR——CPU 想访问哪个非法地址**

| EXCVADDR 的值 | 判断 |
|:---|:---|
| 0x00000000 | 空指针解引用（NULL->member） |
| 0x00000001 ~ 0x000000FF | 野指针/已释放内存复用（本 case：0x5） |
| 看似正常的 RAM 地址 | 数组越界、栈溢出踩指针 |

**第 4 步：寄存器"淘金"+ 生命周期审计**

A0~A15 默认跳过，但扫两类异常值：像指针的小数字、像 ASCII 的十六进制。
本 case：`A14: 0x72727563` + `A15: 0x00746e65` 小端解码 = `"curr"+"ent"` —— 崩溃瞬间 CPU 正拿着字符串 "current"，与 62 行（设备 2 分支）互证。

生命周期审计四问（本 case 的钥匙）：**root 在哪出生？在哪死亡？死后还有人用吗？create 几次 vs delete 几次？** 答案：create 1 次、delete N 次——bug 现形。

### 修复方案

把"序列化 + 删除 + 入队"整段移出 for 循环，每轮只执行一次；配套防御：

```c
// for 循环内只累积属性 ...

/* 循环外：序列化 + 入队 + 清理，每轮一次 */
char *json_str = cJSON_PrintUnformatted(root);
if (json_str == NULL) {                    // 防 OOM
    cJSON_Delete(root);
    continue;
}
// ... strncpy 入队 ...
free(json_str);
cJSON_Delete(root);
root = NULL;                               // free 后置 NULL 防野指针
```

### 防御机制

1. **重构检查项**：对象创建位置挪动时，释放位置必须成对挪动（生命周期跟着搬）
2. **编码规范**：free/delete 后立即置 NULL；cJSON_Add 系列返回值判空
3. **调试武器**：menuconfig 开 Heap poisoning——堆被踩第一时间报，不用等 free 才发现尸体；难复现崩溃用 Core dump 到 Flash 事后分析

### 经验教训

1. **崩溃现场 ≠ 案发现场**：free 崩溃只说明"这里发现了尸体"，真正的破坏发生在更早。排查方向不是"崩溃行写错了吗"，而是"谁更早把内存搞坏了"
2. **崩溃点漂移是堆破坏的指纹**：同一个 bug 两次崩在不同行、不同异常类型，指向内存破坏而非逻辑错误
3. **日志观察是免费线索**："设备 2 还没打印就崩"这一条直接把范围收窄到第 2 轮迭代
4. **free 只是归还，不会置 NULL 你手里的指针**——归还后再 free 是 double free，再读写是 use-after-free，一个 bug 两种死法

### 面试话术

> 遇到 Guru Meditation Error 我按四步走：①看异常类型，判断读错误还是写错误；②看 Backtrace 定位到自己的代码行；③看 EXCVADDR，0 是空指针、小地址是野指针/已释放内存、正常地址是越界；④对可疑指针做生命周期审计——在哪创建、在哪释放、释放后还有谁在用、create 和 delete 次数是否相等。这套流程覆盖绝大多数崩溃；还不够就开 Heap poisoning 或 Core dump。

---

## 问题 9：静默断连瞬间丢 3 条消息——esp_mqtt 发布阻塞 10 秒导致队列溢出

### 现象

T2 断网测试（拔路由器电源）中，离线缓存系统竟然丢了数据：

```
W (70828) STATE_MACHINE: MQTT lost in RUNNING, falling back to MQTT_CONN   ← 断连处理开始
W (74718) APP_TASK: Queue full, message dropped                            ← 丢数据 ×3
W (76818) APP_TASK: Queue full, message dropped
W (78918) APP_TASK: Queue full, message dropped
W (80858) CACHE_TASK: cache offline                                        ← 比断连晚 10 秒
```

三处反常：① 断网续传系统丢消息，违背"宁可重传不可丢"；② `cache offline` 日志比状态机回退晚 10 秒；③ esp_mqtt 持续报 `Writing didn't complete in specified timeout: errno=119`。

### 根因

拔路由器电源属于**静默死亡**——对端没机会发 RST/FIN，socket 不知道链路已死。断网瞬间 cache_task 正用 `mqtt_app_publish` 往这条半死连接发布，esp_mqtt 的写操作傻等到传输层超时才返回——**该超时默认 10 秒**（`mqtt_client.h` 中 `.network.timeout_ms` 的默认值），cache_task 整个任务被卡死 10 秒。

卡死期间队列无人消费，app_task 照常每 2 秒投递：`积压 = 到达速率 × 阻塞时长 = 0.5条/s × 10s = 5 条`，队列深度 5 装满，溢出的 3 条被丢弃。

定性：**一次性过渡窗口 bug**。每次静默断网只有第一个撞上死链路的发布会阻塞；esp_mqtt 判定断连后 `s_online=false`，后续消息直接走 Flash。损失上界 = max(0, 到达速率 × 超时 − 队列深度)。

### 排查路径

1. **用时间差定位阻塞**：`cache offline` 由 cache_task 主循环打印，每轮最多 200ms；状态机 70828 已调 `set_online(false)`，日志 80858 才出现——10 秒空白只能是任务被卡死
2. **找卡在哪**：10 秒恰好等于 esp_mqtt 日志里的 `timeout_ms=10000`，与"发布撞死 socket 等写超时"的推断吻合
3. **对账验证**：阻塞 10 秒内到达 5 条、队列深 5、溢出 3 条，与日志的 3 条 dropped 严丝合缝

### 修复方案

`mqtt_app.c` 配置增加 `.network.timeout_ms = 3000`，把阻塞窗口压进零丢失区间（0.5条/s × 3s = 1.5 条 < 队列深度 5）。

选 3s 的取舍：更短（如 2s）也可以，但给弱网建连多留余量；建连超时失败由状态机 3s 无限重试兜底。超时报错的后果只是消息转存 Flash 后补传——风险被缓存层全部接住。

否决的备选：发布前查连接标志（补丁式，挡不住"标志还 true、socket 已半死"的窗口）。

### 防御机制

1. MQTT 客户端的 network timeout 是断网续传类系统的**必调参数**，默认值 10s 对"快速检测断连"来说太长
2. 队列深度按公式设计：`深度 ≥ 到达速率 × 最大阻塞时长`；超时、频率、深度是同一公式里的联动变量
3. 测试断网必须区分两种死法：**拔电源（静默，无 RST）才触发此类 bug**；后台踢设备（有断连帧）会被秒级感知，测不出来

### 经验教训

1. **"日志迟到了 10 秒"本身就是诊断信息**——周期性日志的节拍被打乱，直接指向任务阻塞，比看报错本身更快定位
2. 默认配置是"通用值"不是"正确值"：esp_mqtt 的 10s 超时对普通应用无所谓，对要求"断网零丢失"的缓存系统就是定时炸弹
3. 架构兜底能把配置风险降级成无害事件：即使超时设得过小"误伤"了弱网发布，后果也只是落盘补传而非丢失——这是 M8 缓存层在断网之外救的第二场
