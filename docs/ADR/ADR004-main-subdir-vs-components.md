# ADR004: main/ 子目录分层 vs 全组件化

## 状态

已决定（2026-07-23）

## 背景

ESP-IDF 采用 Component Architecture，每个组件拥有独立的 `include/`、`CMakeLists.txt`，依赖通过 `REQUIRES` 显式声明。存在两种组织方式：

- **方案 A（全组件化）：** 所有模块（app、drivers、protocol、managers、scheduler）均拆为独立 component，main/ 只保留 app_main.c
- **方案 B（混合方案）：** main/ 内按层级分子目录（app/、drivers/、protocol/ 等），仅将可复用模块（如 mqtt_app）拆为独立 component

## 决策

采用方案 B（混合方案）。

## 理由

1. **项目体量：** 本项目总计约 8-10 个源文件，全组件化需要 8+ 个 CMakeLists.txt 和等量的 include/ 目录，构建系统复杂度超过业务代码本身。
2. **复用性判断：** 组件化的核心价值是跨项目复用。mqtt_app 可被其他项目复用，适合做 component；app_task、poll_scheduler、device_manager 是本网关专属业务逻辑，无复用场景，放 main/ 更自然。
3. **开发效率：** main/ 内子目录共享一个 CMakeLists.txt，新增文件只需加一行 SRCS，迭代速度快。全组件化每加一个模块都要建目录、写 CMake、声明 REQUIRES。
4. **ESP-IDF 官方实践：** 官方示例（mqtt、http_server 等）对单产品项目均采用 main/ 子目录方式，components/ 存放第三方库或可复用模块。
5. **面试表达：** 能解释"为什么没有盲目全组件化"比"我全拆了"更体现工程判断力。

## 后果

- main/ 内通过子目录 + 单一 CMakeLists.txt 管理层级关系
- components/ 目前仅 mqtt_app，后续如有通用模块（如 modbus 库）可再拆出
- 若项目规模增长到 20+ 源文件，或需要跨项目复用某模块，重新评估是否迁移为 component
- 依赖规则通过文档约束（上层→下下单向依赖），而非构建系统强制
