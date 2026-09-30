# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。正式状态由 [dist 清单](../dist/release.manifest.json) 与实际固件哈希确定；源码版本不代表已发布。

## 公共平台收尾

三产品固定相同公共平台提交，具体 SHA 以子模块与正式清单为准。构建接入 `abox_platform_attach_standard_app`；运行入口为 `ABoxAppService_Init/Poll`，通信只有一个 owner。公共 Cloud port 统一响应、启动、关联状态、事件、周期及精确回执；产品提供 Profile、报文、状态和业务 handler。维护使用 `ABoxMaintenanceService` 组合 trial/executor/ConfigStore。OTA 采用现有 Boot V2 App 状态机和公共 bridge 暂停/工作区接线。

产品 Config V5（400 字节） 字节 ABI 保持不变，公共 MQTT V4 冻结。真实轮速、反馈时效、转角、CAN 故障及 OTA 门禁不变；方向改变仍要求零速及连续三次左右实测不超过 20 rpm，2 秒超时撤销使能并锁存故障。正式编译选项沿用原正式版；构建不证明运动或机械反馈。

## 发布与资源

使用公共 `platform/tools/build_product_release.ps1 -ProductRoot <本仓> -AppVersion <版本>`。产品显式声明身份、版本参数与发布合同。只构建 App，消费冻结 Boot；暂存校验及产品门禁全部通过后整体替换 dist，失败保留或恢复原包。

Boot `0x08000000`、App `0x08008000`、State A/B `0x0803E800/0x0803F000`、配置页 `0x0803F800`。冻结 Boot 2.3.0 SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63`。App 上界以产品发布合同为准。固定故障邮箱、RAM/heap/stack 链接断言保持；发布前核对 `tools/service_stack_budget.json` 的 owner 调用链与编译器 `.su`，静态检查不等于运行时栈高水位。

## 验证与边界

公共逻辑仅运行一轮最终主机检查；产品只验证差异，修改或失败后只重跑受影响部分。产品 Profile、codec、证明、安全与幂等的既有证据继续有效。阶段记录和原始日志不改写，源码、主机、ARM、标准板及实车证据分别表述。

共享标准板 UID `05DAFF383734525243054363` 曾验证送餐车候选 1.1.39 空配置入网、独立 get_info 和 MQTT/OTA 就绪；这是前期候选证据，不能代替最终源码实板验证。COM3 是 485 业务接口，接收到了业务数据。当前没有可控整板电源；冷上电/pre-App 停机、完整蜂窝断网及真实持续 OTA 下载/恢复尚未实板验收。Broker kick 和就绪字段不能替代这些验证。没有下发 OTA，也没有进行现场车辆运动或机械验收。

此前已关闭阶段见 [历史记录](operations/history/2026-09-30-cloud-foundation-pre-close.md)。原始 evidence 及既有哈希保持不变。
