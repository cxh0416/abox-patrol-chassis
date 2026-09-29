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

源码候选把底盘入网预期 TLS profile 显式声明为 `0`；公共 Enrollment 默认 `1` 兼容其他产品，profile 不符时拒绝试连。现行底盘服务端入网合同为明文；TLS 凭据 profile `0` 路径仍仅有主机与 ARM 证据。

## 验证证据与现场边界

当前源码候选已接入 Platform 标准 STM32F105/EC800 App Port，Cloud TX 按 MQTT transport operation 精确关联发布完成，App OTA 的 EC800 AT/owner 接线改用 Platform 公共适配器。EC800 ICCID、标准 MQTT runtime 参数、Enrollment 身份格式化与 HTTPS/试连编排继续上收。候选试连现在使用公共 `ABoxMqttTrialExecutor`；产品仍负责 Config V5 codec、心跳证明与安全门禁。39 项产品主机测试、30 项当时的 Platform CTest、ARM 构建和静态栈门禁通过。测试板 UID `05DAFF383734525243054363` 上的禁用执行器候选 `patrol-chassis-0.2.47-cp1` 验证了启动、MQTT READY、Manifest、`get_info`、`sync_state` 及 MCU 重启；禁用输出的 `fi2` 见证候选证明超时后恢复旧 plain 链路及配置页不变；`cp2` 验证公共 OTA 接线的就绪态；`cp5` 通过擦除测试板配置页重新 HTTPS 入网。`patrol-chassis-0.2.48-cp1` 再次经公共 trial executor 完成 HTTPS 入网、候选证明及 Config V5 400 字节落盘；服务端入网达到 verified，独立 `get_info` 返回 200、MQTT READY、ICCID 和 OTA ready。共享测试板随后曾用于清扫车和送餐车。未捕获字面 `RDY`，真实迟到 PUBACK、OTA 下载/安装与占用及完整蜂窝断网仍未实板覆盖；候选未替换 `dist/`，未下发 OTA。记录见[正常路径](operations/evidence/2026-09-29-cloud-port-stage1/index.json)、[异常路径](operations/evidence/2026-09-29-cloud-port-stage1/negative/index.json)、[OTA 接线](operations/evidence/2026-09-29-cloud-port-stage1/ota-adapter/index.json)、[公共 Enrollment 实板证据](operations/evidence/2026-09-29-platform-enrollment-service/index.json)和[公共试连执行器实板证据](operations/evidence/2026-09-29-platform-trial-executor-patrol/index.json)。

正式发布脚本现先运行产品 `artifact_store.py` 的专属校验，再使用 Platform 公共发布器按 `tools/release_contract.json` 检查冻结 Boot、App 向量和上界、版本标记、Manifest/哈希与 Full 精确拼接，并在暂存复核后整体替换 `dist/`。三个产品现有正式包均只读通过该合同校验；公共发布器的失败保留及回滚测试通过，尚未用本候选执行正式发布。最新 Platform 32 项 CTest 和本产品 39 项主机测试通过，ARM Release 构建保持 RAM 61,432/65,536 字节。

当前源码进一步改用 `abox_platform_attach_standard_app` 统一标准板组件接入，并由 `ABoxMqttV4_BuildTopic()` 构造五个公共 Topic；产品 `/internal/*` 合同、Config V5 和运动准入未变。39 项产品主机测试、32 项 Platform CTest 与 ARM Release 构建通过，RAM 仍为 61,432/65,536 字节；这一步未在测试板重刷底盘候选。

前一底盘台架候选 `patrol-chassis-0.2.48-cp2` 将 Manifest → Heartbeat → 完整状态的启动游标与连接 generation、发布 operation 回执接入 Platform `ABoxCloudBootstrap`。产品继续管理动态遥测频率、响应、维护事务、CAN 业务和安全门禁；本台架构建明确关闭执行器、车辆电源输出及只读 CAN 诊断。39 项产品主机测试、32 项 Platform CTest 和 ARM Release 构建通过，RAM 61,432/65,536 字节、Flash 120,004 字节。共享测试板保留冻结 Boot，刷入匹配 App/State，擦除测试用 Config V5 与恢复页后重新入网；底盘合同请求达到 verified，独立 `get_info` 为 200、MQTT READY、ICCID 存在、OTA ready。Broker 踢线后订阅采集到当前版本 Manifest → Heartbeat → `chassis_status`，随后约 5 秒心跳继续；`sync_state` 仍先响应 200 再发送同 requestId 状态。只读配置头确认 Config V5 magic、版本及 400 字节长度。原始记录与 SHA256 见[紧凑 Cloud 启动实板证据](operations/evidence/2026-09-29-platform-cloud-bootstrap-patrol/index.json)。测试板当时运行 cp2 候选，`dist/` 正式包不变；没有真实 CAN、车辆输出、完整蜂窝断网、迟到 PUBACK 或真实 OTA 安装验收。

后续 `patrol-chassis-0.2.48-cp3` 将 MQTT runtime 与既有 `MQTT_Core` 间的八项纯接线回调改用 Platform `ABoxMqttRuntimePort_BindCore()`，网络变更撤销、时间、CA 与试连策略仍在各自职责层。39 项产品主机测试、Platform 33 项 CTest 和 ARM Release 构建通过，RAM 仍为 61,432 字节。共享测试板刷入匹配 App/State 并保留 Config V5，冻结 Boot 校验成功。首次 `get_info` 在重连窗口超时，重新就绪后独立查询得到 200、MQTT READY、OTA ready；Broker 踢线后观察到 Manifest → Heartbeat → `chassis_status`，`sync_state` 仍先响应再发送关联状态。记录见[MQTT runtime 接线实板证据](operations/evidence/2026-09-29-platform-runtime-core-port-patrol/index.json)。测试板当时运行 cp3 禁用输出候选，正式 `dist/` 未替换；Broker 踢线不能代替完整蜂窝断网，车辆 CAN、执行器及机械反馈未验收。

`patrol-chassis-0.2.48-cp4` 又将标准板 STM32 UID 与冻结 Boot 描述符读取交给 Platform Hardware Port，产品仍提供 VID 判据、fallback 前缀、专属 hardwareContract、App 版本与 Config V5 codec。39 项产品主机测试、Platform 34 项 CTest 和 ARM Release 构建通过，RAM 仍为 61,432 字节。共享测试板在执行器、车辆电源及只读 CAN 诊断均关闭的构建下刷入匹配 App/State，擦除测试 Config V5 与恢复页后重新入网；底盘专属请求达到 verified，独立 `get_info` 返回 200、MQTT READY、OTA ready。原始证据见[标准硬件身份实板记录](operations/evidence/2026-09-29-platform-enrollment-identity-patrol/index.json)。测试板当时运行 cp4；正式 `dist/`、Config V5 ABI 和冻结 Boot 未变。

`patrol-chassis-0.2.48-cp5` 将产品 TX 的发布类型、operation 和连接代次交给 Platform 紧凑跟踪器；产品仍决定动态遥测、响应、心跳证明、日志和运动安全语义。39 项产品主机测试、Platform 34 项 CTest、ARM Release 与静态栈门禁通过，RAM 61,432/65,536 字节。标准测试板刷入匹配 App/State，擦除测试 Config V5 与恢复页后按底盘合同重新入网并达到 verified；独立 `get_info` 为 200/READY/OTA ready。`sync_state` 先响应再上报同 requestId 状态；Broker 踢线后观察到 Manifest → Heartbeat → `chassis_status`，随后心跳继续。App、冻结 Boot 哈希和原始记录见[紧凑 TX 跟踪实板证据](operations/evidence/2026-09-30-platform-cloud-tx-tracker-patrol/index.json)。本构建禁用执行器与车辆电源输出；Broker 踢线不等于完整蜂窝断网或真实迟到 PUBACK 注入，未验证车辆 CAN、机械反馈与 OTA 安装；正式 `dist/` 未替换。

`patrol-chassis-0.2.48-cp6` 在 MQTT ready 下降时让公共 TX tracker 失效，避免旧 operation 占住新连接的发送槽。39 项产品主机测试、Platform 34 项 CTest、ARM Release 和静态栈门禁通过，RAM 仍为 61,432 字节。测试板刷入匹配 App/State，擦除测试 Config V5 与恢复页后按底盘合同重新入网并达到 verified；独立 `get_info` 为 200/READY、OTA ready，`sync_state` 先响应再上报。Broker 踢线后再次观察到 Manifest → Heartbeat → `chassis_status`，随后心跳继续。哈希、原始刷写和回归记录见[ready 下降实板证据](operations/evidence/2026-09-30-platform-cloud-ready-loss-patrol/index.json)。本构建禁用执行器与车辆电源输出；完整蜂窝断网、指定迟到 PUBACK 注入、车辆 CAN/机械反馈和 OTA 安装尚未验证，正式 `dist/` 未变。

先前正式包的 39 项产品主机测试、23 项 Platform CTest、ARM 构建和静态栈门禁已通过；本轮候选仍需后续阶段回归，不能以源码或测试板结果替代正式包证据。

前一轮 0.2.46 通信专项只操作测试板 UID `05DAFF383734525243054363`，J-Link `69730334`，执行时确认 COM11；按用户指示未做刷前备份。当次测试结束时曾换回巡防底盘正式 App，按底盘硬件合同重新入网并撤销旧清扫车凭据；此后测试板又切换为上文所述 cp2 候选。无真实 CAN 节点，因此平台 VID 任务收到同一 requestId 的响应与落库报告后，明确拒绝轮速反馈不新鲜，未派发改名命令；登录后的页面也显示具体原因。该版直接通信专项完成 plain/TLS 切换、VID 往返、无效候选恢复、重启持久化；重启由平台任务验证 200 REBOOTING、uptime 1307681→17591 ms、业务上线及独立身份查询。底盘单设备 Broker 断线后约 45.5 秒恢复，新业务心跳与独立查询成功；最终 ACL 精确匹配旧身份的 11 条规则，仅底盘当前身份会话在线。通信专项结果和不可变记录见[本轮证据](operations/evidence/2026-09-29-maintenance-transactions/index.json)。平台停车正向路径由隔离主机夹具覆盖，不伪造生产快照。

现场车辆 `PATROL-05D3FF343733574E43043550` 仍以既有 `0.2.45` OTA 确认为当前现场记录，本轮未对其执行改名、刷写、运动或电源操作。0.2.47 源码和构建不构成整车运动、真实转角、继电器触点、电源或 OTA 下发验收。本车无独立接触器反馈输入，`powerOutputOn` 仅表示 MCU 输出命令。

## 当前维护入口

- [远控与标定维护](operations/架空行走与平台远控实现.md)：公共 V4 控制会话及独占中位标定；不再使用旧 bench 许可和持久 remote_enabled 授权。
- [源码模块与依赖](implementation/源码模块与依赖.md)、[Config V5 与掉电恢复](implementation/配置V5与掉电恢复.md)。公共能力使用 ABox；Config V3/V4 不进入正式运行路径。
- [本轮前的状态与历史验收索引](operations/history/2026-09-29-current-before-maintenance-close.txt)。旧版本参数、工作包过程和历史实测不得直接作为当前操作步骤。
