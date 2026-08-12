# ADR001: 采用 Queue 实现 App Task 与 MQTT Task 的解耦通信

## 状态
已采纳

## 背景
App Task 负责从 Device Manager 读取数据并封装 JSON，MQTT Task 负责将数据发布到 Broker。两者需要传递数据，但不应直接耦合。

## 决策
采用 FreeRTOS Queue 作为两个任务间的通信机制。App Task 为生产者，MQTT Task 为消费者。

## 备选方案

| 方案 | 优点 | 缺点 |
| :--- | :--- | :--- |
| Queue（采用） | 天然线程安全、支持阻塞等待、解耦清晰 | 需要额外内存存放队列缓冲区 |
| 全局变量 | 实现简单 | 需要额外加锁、耦合度高、不易测试 |
| 事件组（EventGroup） | 适合多事件通知 | 只能传标志位，不能传数据 |
| 直接函数调用 | 最简单 | 两个任务强耦合，MQTT 需要等待 App 处理完 |

## 为什么选择该方案
Queue 是 FreeRTOS 提供的标准 IPC 机制，天然支持生产者-消费者模型。App Task 和 MQTT Task 通过 Queue 解耦后，各自独立运行，互不阻塞。MQTT Task 阻塞在 `xQueueReceive` 上，有数据时才工作，没有数据时不消耗 CPU。

## 影响
- Queue 深度需要根据上报频率和网络延迟合理设置
- 队列满时需要有丢弃策略（超时发送 + 日志警告）
