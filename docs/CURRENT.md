# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。正式版本以 [release.manifest.json](../dist/release.manifest.json) 和实际固件哈希为准。

## 当前正式版本

- 固件：`patrol-chassis-0.2.47`，App SHA256 `B64A600A003507873E4FC14FEDEEB557D2A259272A376E96D54457648F83CEC5`。
- Product 构建提交以 `dist/release.manifest.json` 为准；Platform：`9c3b90826dc90de32ca28fb392da2be1beac5580`；两者构建时均为 clean。
- 转向与行走执行器已编译，运动输出支持开启；启动运动许可为 false，未标定、反馈失效及既有安全门禁继续禁止运动。
- 冻结 Boot `abox-boot-2.3.0` 未改，SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63`。
- Boot `0x08000000`，App `0x08008000..0x0803DFFF`，配置恢复页 `0x0803E000`，Boot State A/B `0x0803E800` / `0x0803F000`，Config V5 `0x0803F800`。

## 板载 I/O 公共化

当前正式版接入公共 `abox::board_io` 和 STM32F105 板型适配：3 路 PVIN、1 路继电器、8 路光耦输入、3 个 LED 有统一接口。原理图映射与消抖说明见 [硬件与安全边界](hardware/硬件与安全边界.md) 和平台 `docs/internal/board_io.md`。本产品仅授权 PA0 OUT1 电源输出；其余输出保持关闭，LED 无业务灯义。8 路输入目前仅采样，不作为运动或接触器反馈。输出读回仅确认 MCU 锁存位。主机回归、平台测试和 ARM 构建不能代替实板电气验收。

## 维护事务与平台配套

内部 `get_info.maintenanceProtocols` 分别声明 `mqttConfigTransaction: 1` 与 `vidChangeTransaction: 1`；它们是协议语义版本，不表示允许运动或允许写入。公共 MQTT V4 保持冻结。平台 `0.4.168` 已部署，版本名单已退出准入；每次任务读取实际配置、协议声明和托管凭据，再进行关联状态刷新。设备受理、保存/恢复终态、业务上线和独立读回分别记录，未知结果保持互斥占用且只读核对。

本次还修复了 get_info 发布确认未完成时 sync_state 响应丢失的竞态：同步请求等待发送槽，响应与关联状态报告均可到达平台。公共 Platform 增加 plain 连接的时钟读取、HTTPS 入网 PDP 准备及会话开始前失败重试。产品信息响应预算为 1536 字节。

## 验证证据与现场边界

39 项产品主机测试、23 项 Platform CTest、ARM 候选与正式构建、静态栈门禁通过。正式包由暂存目录验证后整体替换 dist，候选与正式 App 哈希一致。

前一轮 0.2.46 通信专项只操作测试板 UID `05DAFF383734525243054363`，J-Link `69730334`，执行时确认 COM11；按用户指示未做刷前备份。测试板已换回当前巡防底盘正式 App，按底盘硬件合同重新入网，旧清扫车凭据已撤销。无真实 CAN 节点，因此平台 VID 任务收到同一 requestId 的响应与落库报告后，明确拒绝轮速反馈不新鲜，未派发改名命令；登录后的页面也显示具体原因。该版直接通信专项完成 plain/TLS 切换、VID 往返、无效候选恢复、重启持久化；重启由平台任务验证 200 REBOOTING、uptime 1307681→17591 ms、业务上线及独立身份查询。底盘单设备 Broker 断线后约 45.5 秒恢复，新业务心跳与独立查询成功；最终 ACL 精确匹配旧身份的 11 条规则，仅底盘当前身份会话在线。通信专项结果和不可变记录见[本轮证据](operations/evidence/2026-09-29-maintenance-transactions/index.json)。平台停车正向路径由隔离主机夹具覆盖，不伪造生产快照。

现场车辆 `PATROL-05D3FF343733574E43043550` 仍以既有 `0.2.45` OTA 确认为当前现场记录，本轮未对其执行改名、刷写、运动或电源操作。0.2.47 源码和构建不构成整车运动、真实转角、继电器触点、电源或 OTA 下发验收。本车无独立接触器反馈输入，`powerOutputOn` 仅表示 MCU 输出命令。

## 当前维护入口

- [远控与标定维护](operations/架空行走与平台远控实现.md)：公共 V4 控制会话及独占中位标定；不再使用旧 bench 许可和持久 remote_enabled 授权。
- [源码模块与依赖](implementation/源码模块与依赖.md)、[Config V5 与掉电恢复](implementation/配置V5与掉电恢复.md)。公共能力使用 ABox；Config V3/V4 不进入正式运行路径。
- [本轮前的状态与历史验收索引](operations/history/2026-09-29-current-before-maintenance-close.txt)。旧版本参数、工作包过程和历史实测不得直接作为当前操作步骤。
