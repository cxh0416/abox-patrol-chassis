# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。发布事实以正式 manifest 为准；候选状态以对应构建物和 evidence 为准。历史过程不在此重复。

## 标定与远控简化固件已单车 OTA

当前新流程已完成源码、平台和主机改造：正常远控直接建立公共 V4 控制会话；在线标定只走独占中位会话；旧 bench 许可和持久 `remote_enabled` 授权已移除。Config V5 的原授权字节保留在原位置并作为保留字节。平台 `0.4.158` 已部署；2026-09-28 在目标车完成 `0.2.45` OTA 确认后，平台远控总开关已开启。本车无独立接触器反馈输入，`powerOutputOn` 只表示 MCU 输出命令。

`dist/` 最新正式版为 `patrol-chassis-0.2.45`，App SHA256 `7A4E5FE56F15E588B0672DE06A7731447D376FB1008E9F019BC175CD768AF666`；转向和行走执行器已编译，`actuation_enabled=true`，正式 App 与运动候选逐字节一致。正式包包含现场分支的 CAN Bus-Off 恢复、±500 rpm 行走上限和 300/150/75 rpm 转向轮廓。既有 [0.2.12 行走换向实测](operations/evidence/2026-09-08-bench-drive-0212.json)和 [0.2.31 转向实测](operations/evidence/2026-09-15-031-steering-speed/adaptive-validation/summary.md)构成本轮已验证硬件的基础。产品主机测试、Platform CTest 21 项、ARM 候选与正式重建通过。目标车 `PATROL-05D3FF343733574E43043550` 已由 `0.2.44` OTA 到 `0.2.45`，独立 `get_info` 返回 Boot `CONFIRMED`、OTA 空闲；状态报告显示标定有效、停车、实测转速为零、输出命令关闭。平台远控入口现已开启，但尚无活动控制会话。当前版本的控制会话、CAN/编码器和实际停车运动回归尚未执行，OTA 与状态响应不能替代运动验收。当前步骤见[远控运维](operations/架空行走与平台远控实现.md)。

## Current Release / Candidate

| 项目 | 当前值 | 权威来源 |
|---|---|---|
| 最新正式版 | `patrol-chassis-0.2.45`，运动输出 ON | `dist/release.manifest.json` |
| 正式 App SHA256 | `7A4E5FE56F15E588B0672DE06A7731447D376FB1008E9F019BC175CD768AF666` | 正式 manifest |
| 当前源码版本 | `patrol-chassis-0.2.45`；主机测试、ARM 构建及单车 OTA 确认通过，运动回归待执行 | 根目录 `VERSION`、正式 manifest |
| WP7.3 clean ARM Release App SHA256 | `DE38325CE586CC1F703FAD78C6BC4D219450CDE110A5E56400897A237A6C7251` | [WP7.3 evidence](operations/evidence/2026-09-23-mqtt-wp73/index.json)；实测候选，非 `dist/` 正式版 |
| WP7.2 最新实测 Debug App SHA256 | `5AE13A1367345515245F9D2C694710BA3140DD18F9DEC3FFF81BE3ABD471B5D4` | `out/logs/wp72-success-app.bin` 与 WP7.2 index；测试构建，非正式版 |
| 当前改造分支 Platform gitlink | `4f97ca9d155f4be5869ab6b2b911e9c2ae5b909c` | 当前源码及正式 manifest |
| WP7.2 checkpoint App SHA256 | `6EE9E4188E001B8581CA4198F9A4B7D1D0D91F132273CA0955325C1295BB450E` | clean ARM Release candidate；非 `dist/` 正式版 |
| 冻结 Boot | `abox-boot-2.3.0`，SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63` | 正式 manifest；Boot `0x08000000` |

`0.2.37` 已移除内部 `/internal/config` 的上下电动作；平台继续使用公共 V4 `vehicle_chassis 1.0` 上下电命令。产品 38 项主机测试、Platform 21 项 CTest、底盘相关 C 测试和 ARM Release 构建通过。正式包未下发 OTA，继电器触点与整车上下电尚未现场验收。此前 `0.2.36` 的入网、业务上线和恢复实机证据见[现场记录](operations/evidence/2026-09-24-enrollment-0236/index.json)。

WP7.3 的 `0.2.32` 候选与旧正式版曾使用相同版本字符串，但哈希不同。当前正式状态只能由 `dist/release.manifest.json` 及其固件哈希确认；`0.2.34` 正式打包不等于设备已升级。
此前 `0.2.33` 的测试、源码提交与现场边界见[正式打包记录](operations/history/2026-09-23-0.2.33正式打包.md)。

## 统一入网改造状态（2026-09-24）

当前正式版接入 Platform `ABoxEnrollment`、EC800 HTTPS POST 与首次 MQTT Trial，出厂 Config V5 的 MQTT 字段为空。已保存且 MQTT 字段完整的逐车 V5 配置继续直接运行；新设备和整片擦除后的设备按芯片 UID 与当前固件的产品合同申请，底盘出厂临时 VID 为 `PATROL-` 加 UID。审批下发仅更新 VID、MQTT 字段及原有入网标记，不恢复被擦除的标定；未标定车辆仍禁止运动。

入网改造初次发布时三个产品使用 Platform `b51bfb163bf84c1269091d71895c88b4e7a66b72`；底盘 `0.2.34` 当时只有主机和构建证据。此后底盘 `0.2.36` 的 UID 申请、管理员审批、第五代凭据领取、Broker 和业务在线、重启及单设备断线恢复已完成实机复验。跨产品重刷、断电注入和 OTA 仍未验收。

## 公共通信接入改造状态（底盘现场已验）

当前巡防底盘源码指向 Platform `4f97ca9d155f4be5869ab6b2b911e9c2ae5b909c`，已改用公共 EC800 MQTT transport，移除产品私有连接/收发状态机及接收解析器。唯一 UART/AT 分流、产品 Topic/身份薄适配、runtime 关闭委托、OTA 工作区借还与网络变化撤权已在源码接通。产品主机回归 38/38、Platform CTest 21/21 和 ARM Release 链接通过；正式 App RAM 61,432 B、Flash 126,896 B，静态栈门槛通过。模组、Broker、业务服务端、TLS 失败候选恢复、重启与单设备断线已复验；运动、电源和 OTA 未执行。

送餐车与清扫车此前接入同一公共 transport 时各自指向 Platform `b212b33417879aa0667ff4bdba5ec586568492c0`；本轮仅更新底盘 gitlink。其他产品的现场状态以各自仓库为准。底盘本轮验证包含真实 Broker 收包和业务服务端心跳，不以 MQTT ACK 代替业务在线证明。

## 2026-09-24 底盘入网与恢复现场验收

UID `05D3FF343733574E43043550` 的最新申请 `c165e71a-e8d4-4c7d-b41b-795e983bf4f9` 在服务端为 `verified`，凭据代次 5；设备使用正式 VID `PATROL-05D3FF343733574E43043550`。审批后新版固件领取凭据、订阅并发布完整 V4 心跳，业务设备记录更新为在线。服务端已部署 `0.4.146`，worker 按 Broker 收到的心跳确认入网；审批状态、Broker 连接和业务在线分别核对。

首个失败环节是 TLS 失败候选的 `AT+QMTOPEN=1` 收到 `+QMTOPEN: 1,-1` 后，未打开 socket 却继续发送 `QMTDISC`/`QMTCLOSE`，两条 `ERROR` 导致 AT 隔离。公共 transport 现在仅对明确的 `-1` 未打开结果进入排空/重试；不确定失败仍保留隔离。正式 `0.2.36` App 已用 J-Link App-only 写入并回读校验；失败 TLS 候选先回 `202`，恢复原连接后同一 requestId 回 `500 MQTT_CANDIDATE_RESTORED`，Config V5 页实读 SHA256 前后一致。独立 MCU 复位及单设备 Broker 断开后均自动恢复业务在线。详情见[脱敏现场证据与受控原件索引](operations/evidence/2026-09-24-enrollment-0236/index.json)。

维护场景：保留配置升级时先备份并校验逐车 Config V5，升级后应沿用现有凭据；同产品整片擦除前归档配置、标定、Boot State 和证据，重刷冻结 Boot、App、Boot State 及 CA/UFS 前置条件，审批后核对新凭据、保存回读及重启上线；跨产品整片擦除还须确认管理员审批目标产品合同、Broker Topic 权限、旧凭据和旧会话撤销，随后单独恢复目标产品所需标定。现场原始证据归入 `docs/operations/evidence/`，不得覆盖历史记录。

## Current Phase

现场 0.2.38 至 0.2.44 的 CAN、驱动故障、OTA 与速度试验过程见[历史现场记录](operations/history/2026-09-27-28-底盘现场跟进.md)和[原始证据](operations/evidence/2026-09-28-busoff-recovery/index.json)。当前软件回归仍需验证实际反馈、停止和换向。

```text
WP7.1 CLOSED          A/B/C/D1 passed
WP7.2 CLOSED
WP7.3 CLOSED
D2 REAL VEHICLE NOT STARTED
```

## Current Checkpoint

```text
Patrol checkpoint commit = a8b952663a8e5c109a5c30da889d3cef465d0ad5
Platform commit = 26a8ab3687336a39f30e578b713b43edbb3488b0
App SHA256 = 6EE9E4188E001B8581CA4198F9A4B7D1D0D91F132273CA0955325C1295BB450E
```

该 App 是可重复构建的旧 checkpoint candidate，不是当前 `dist/` 正式包。

进入 WP7.3 前已按 ABox Platform、产品仓通用能力和 Patrol 产品能力完成命名收口。当前产品 Flash Config 只接受 V5；V3/V4 仅属于开发历史，不进入正式运行路径。公共 MQTT V4 与 Config V4 是不同版本概念，MQTT V4 未修改。WP7.1、WP7.2、WP7.3 均为 CLOSED。正式 `set_mqtt` 已完成旧连接 202 发送门禁、VID/Topic RAM Trial、proof 后 Config V5 保存及回读、失败恢复与最终同 requestId 响应。实机 plain→TLS、TLS→plain、失败 candidate、VID 切换及失败恢复、重启与物理回读通过；主机、Platform CTest、bench、clean ARM Release 与静态 RAM/栈门禁通过。实测后原 App、Boot State A/B 与 Config 已物理回读确认逐字节恢复；这是 WP7.3 验收时的设备状态。随后以 `0.2.33` 重新构建并正式打包，尚未对该正式包执行 OTA 或现场复验。详见 [WP7.3 evidence](operations/evidence/2026-09-23-mqtt-wp73/index.json)。

WP7.2 最新实机状态：

- plain → plain、plain → TLS、TLS → TLS、TLS → plain：2026-09-23 关闭注入后的实机复测均 `COMMITTED`
- TLS candidate failure → restore plain：PASS
- subscribe timeout、business proof timeout：实机 `TIMEOUT → RESTORING → plain READY`，reason=2
- cancel：实机 `VERIFYING → RESTORING → plain READY`，reason=3
- restore failure：实机 `RESTORE_FAILED`，runtime `FAILED`，MQTT 安全门为 0；未见隐式 plain fallback
- 成功矩阵之后重新物理读取 `0x0803F800`，`config-after-wp72.bin` SHA256 为 `A1E93C70B13B43FBF149402EE03FA8AC5A1B1FCF6A8004F2301C210DDD978835`，与基线一致；Boot 物理回读也与冻结产物一致

2026-09-23 WP7.2 补采：COM3 为 FTDI USB Serial Port，按 UART5 `19200 8N1` 采集。原始串口记录包含 TLS trial 的 `QMTOPEN/QMTCONN`、PREPARED/CONNECTED/SUBSCRIBED、proof 和 commit；EMQX 原生 trace 包含同一 clientId 的 CONNECT/CONNACK 与 9 组 SUBSCRIBE/SUBACK。该记录只归属 WP7.2；正式版身份仍以 `dist/` manifest 为准。

## Frozen MQTT Test Baseline

### Plain

```text
public endpoint: 36.153.162.171:19683
client: 0
```

### TLS

```text
public endpoint: zxwl.ntiov.com:19688
NAT backend: 192.168.4.196:8883
client: 1
TLS context: 2
TLS profile: 0
```

`19688` 是设备公网 TLS endpoint；`8883` 是后端 EMQX TLS listener，不是设备公网测试入口。没有基础设施变更证据时，不得探索 `19888`、`19684` 等相邻端口。若该基线失效，先证明 NAT、Broker、证书或环境发生变化，再修改本节。

来源：[WP7.1 B Broker trace](operations/evidence/2026-09-22-mqtt-wp71-a/broker-tls-trace.jsonl)、[当前 MQTT 7.x 执行文档](implementation/MQTT双模式工作包7-8执行与验收.md)和[WP7.2 evidence](operations/evidence/2026-09-22-mqtt-wp72/)。

## Authoritative Documents

- 公共业务接口：[巡防车底盘业务 Profile 1.0](protocols/A-BOX巡防车底盘业务协议-V4.0.md)；公共 MQTT V4 通用合同由该文档链接到 Platform 权威版本。
- 目标内部运维接口：[巡防车底盘内部运维协议 V2.3](protocols/A-BOX内部运维协议V2.3.md)，报文 `version` 仍为 `2.0`；产品 Flash Config V5 是独立版本。该合同适用于当前正式固件与新平台；现场单车已切换，远控运动回归仍待执行。旧接口修订留在[历史目录](operations/history/A-BOX内部运维协议V2.2.md)。
- 硬件与安全：[硬件与安全边界](hardware/硬件与安全边界.md)。
- 当前实现架构：[源码模块与依赖](implementation/源码模块与依赖.md)。
- 当前产品配置：[配置 V5 与掉电恢复](implementation/配置V5与掉电恢复.md)。
- 当前 MQTT 执行计划：[MQTT 双模式工作包 7-8 执行与验收](implementation/MQTT双模式工作包7-8执行与验收.md)。
- 当前车辆参数与标定：[车型参数与台架转向实现](operations/车型参数与台架转向实现.md)、[逐车标定与参数记录](operations/巡防车底盘逐车标定与参数记录.md)、[CANopen 执行约束](operations/CANopen执行约束.md)。
- Evidence 导航：[证据总索引](operations/evidence/README.md)。

完成的工作包、带日期的阶段结果和旧需求只在 `operations/history/` 查阅，不作为当前规范。
