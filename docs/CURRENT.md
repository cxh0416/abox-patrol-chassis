# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。正式状态由 [dist 清单](../dist/release.manifest.json) 与实际固件哈希确定；源码版本不代表已发布。

## 当前正式包

版本 `patrol-chassis-0.2.51`，App SHA256 `6A049FF261D8D7B464E9D689DADC5B245D13D1389C131DA9F251A4231DE71EF6`。公共平台固定 `62b26893e8f7a94e090f4dbf6936a4d1026df5eb`。构建与暂存校验通过后已整体替换 dist；正式打包不代表已下发 OTA 或现场验收。

## 公共平台收尾

源码候选 `patrol-chassis-0.2.52` 与公共平台 `1f9f031` 默认使用 TLS/profile `0`；空配置仍通过既有入网审批获取逐设备凭据。配置生成器默认 TLS，Python `--no-mqtt-tls-enabled` / PowerShell `-PlainMqtt` 保留显式明文调试，已有 Config V5 不强制转换。时间修复、运动与电源安全准入保持不变。

该候选按声明的正式编译选项完成 ARM 构建，主机回归通过；同型号 EC800E 台架主机名负向测试失败，尚未正式打包或进行本产品完整 TLS/授时实机验收，当前正式 dist 保持 `0.2.51`。此产品仍处于开发测试，不建立批量迁移系统。正式启用 TLS 前必须完成设备 CA、主机名、有效期、ACL 和失败恢复验收。

三产品固定相同公共平台提交，具体 SHA 以子模块与正式清单为准。构建接入 `abox_platform_attach_standard_app`；运行入口为 `ABoxAppService_Init/Poll`，通信只有一个 owner。公共 Cloud port 统一响应、启动、关联状态、事件、周期及精确回执；产品提供 Profile、报文、状态和业务 handler。维护使用 `ABoxMaintenanceService` 组合 trial/executor/ConfigStore。OTA 采用现有 Boot V2 App 状态机和公共 bridge 暂停/工作区接线。

产品 Config V5（400 字节） 字节 ABI 保持不变，公共 MQTT V4 冻结。真实轮速、反馈时效、转角、CAN 故障及 OTA 门禁不变；方向改变仍要求零速及连续三次左右实测不超过 20 rpm，2 秒超时撤销使能并锁存故障。正式编译选项沿用原正式版；构建不证明运动或机械反馈。

## 发布与资源

使用公共 `platform/tools/build_product_release.ps1 -ProductRoot <本仓> -AppVersion <版本>`。产品显式声明身份、版本参数与发布合同。只构建 App，消费冻结 Boot；暂存校验及产品门禁全部通过后整体替换 dist，失败保留或恢复原包。

Boot `0x08000000`、App `0x08008000`、State A/B `0x0803E800/0x0803F000`、配置页 `0x0803F800`。冻结 Boot 2.3.0 SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63`。App 上界以产品发布合同为准。固定故障邮箱、RAM/heap/stack 链接断言保持；发布前核对 `tools/service_stack_budget.json` 的 owner 调用链与编译器 `.su`，静态检查不等于运行时栈高水位。

正式构建 Flash 138,392 字节、RAM 地址覆盖 61,432/65,536 字节。维护保存链已知帧加 1,024 字节保留量不超过 4,096 字节；此结果为静态估计。

## 验证与边界

本地空闲回中已接入产品源码：没有有效远控目标（包括租约到期）时，行走先确认停车并撤使能，安全条件满足后由本地转向执行器回中，不恢复行走授权；两路新鲜转角连续三组在 ±1° 内结束。新有效远控可接管，显式 stop / 下电及安全故障取消；总过程 30 秒超时进入执行故障。行为与流程见 [远控与本地回中](operations/架空行走与平台远控实现.md)。产品 40 项 Python 与 C 夹具通过；覆盖真实控制仲裁、租约撤销后回中、行走停车等待、接管、停车竞态、门禁、双角度新样本与超时。现场诊断原始材料见 [回中中断证据](operations/evidence/2026-09-30-remote-center-timeout/index.json)。新逻辑尚未下发 OTA，也未做真实转向、回中或整车机械验收；不能将主机测试视为现场通过。

公共最终 45 项 CTest 通过；本产品 40 项 Python 与产品 C 夹具通过。三产品正式 ARM Release 与声明的维护/授时调用链栈检查通过。公共逻辑仅运行一轮最终主机检查；产品只验证差异，修改或失败后只重跑受影响部分。产品 Profile、codec、证明、安全与幂等的既有证据继续有效。阶段记录和原始日志不改写，源码、主机、ARM、标准板及实车证据分别表述。

共享标准板 UID `05DAFF383734525243054363` 曾验证送餐车候选 1.1.39 空配置入网、独立 get_info 和 MQTT/OTA 就绪；这是前期候选证据，不能代替最终源码实板验证。COM3 是 485 业务接口，接收到了业务数据。当前没有可控整板电源；冷上电/pre-App 停机、完整蜂窝断网及真实持续 OTA 下载/恢复尚未实板验收。Broker kick 和就绪字段不能替代这些验证。没有下发 OTA，也没有进行现场车辆运动或机械验收。

此前已关闭阶段见 [历史记录](operations/history/2026-09-30-cloud-foundation-pre-close.txt)。原始 evidence 及既有哈希保持不变。

## 时间模型修复

源码版本 patrol-chassis-0.2.51 已接入公共 TIM2 monotonic、ClockSnapshot 与可信 UTC；CCLK只bootstrap，TLS上线/重连三次采样并按误差预算周期复校。有效期上限只校验 expiresAt-request.timestamp，已接受请求先重放，UTC及monotonic双保护阻止校时前跳提前淘汰。公共业务包络和Config ABI保持。时间敏感新请求没有可信平台授时时返回4003；安装前必须部署授时服务及精确ACL。

本产品只完成主机回归和ARM构建；共享送餐车标准板验证5次Flash保存403ms及32秒关中断不丢时，不能替代本产品实车验证。动作重放为RAM模型，跨重启持久账本仍需独立落实；本轮不宣称跨重启幂等验收。
