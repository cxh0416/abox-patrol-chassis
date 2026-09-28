# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。发布事实以正式 manifest 为准；候选状态以对应构建物和 evidence 为准。历史过程不在此重复。

## Current Release / Candidate

| 项目 | 当前值 | 权威来源 |
|---|---|---|
| 最新正式版 | `patrol-chassis-0.2.36` | `dist/release.manifest.json` |
| 正式 App SHA256 | `B12298E9BE917FA63F6CFA8F3C510A51187EB1A20BE672B54CDEEAE60501D0B9` | 正式 manifest |
| 当前源码版本 | `patrol-chassis-0.2.36`；入网、业务上线和恢复已现场复验 | 根目录 `VERSION`、正式 manifest 与 [现场证据](operations/evidence/2026-09-24-enrollment-0236/index.json) |
| WP7.3 clean ARM Release App SHA256 | `DE38325CE586CC1F703FAD78C6BC4D219450CDE110A5E56400897A237A6C7251` | [WP7.3 evidence](operations/evidence/2026-09-23-mqtt-wp73/index.json)；实测候选，非 `dist/` 正式版 |
| WP7.2 最新实测 Debug App SHA256 | `5AE13A1367345515245F9D2C694710BA3140DD18F9DEC3FFF81BE3ABD471B5D4` | `out/logs/wp72-success-app.bin` 与 WP7.2 index；测试构建，非正式版 |
| 当前改造分支 Platform gitlink | `4f97ca9d155f4be5869ab6b2b911e9c2ae5b909c` | 当前源码及正式 manifest |
| WP7.2 checkpoint App SHA256 | `6EE9E4188E001B8581CA4198F9A4B7D1D0D91F132273CA0955325C1295BB450E` | clean ARM Release candidate；非 `dist/` 正式版 |
| 冻结 Boot | `abox-boot-2.3.0`，SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63` | 正式 manifest；Boot `0x08000000` |

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

2026-09-27 状态上报复验：设备 `PATROL-05D3FF343733574E43043550` 的 `0.2.36` 固件实际已发送 `/report`，平台旧解析器因角度字段 `0.000` 拒绝整条报文。平台 `device-ota-service 0.4.147` 已修复并部署，17:49 CST 新状态成功落库；电源输出锁存为低、节点和反馈未就绪。本次未修改或烧录固件，正式 `dist/` 仍为 `0.2.36`；未下发电源或运动命令，不代表控制器供电、CAN 或真实运动已验收。原始证据位于平台仓 `docs/operations/evidence/2026-09-27-patrol-decimal-report/`。

2026-09-27 台架远控继续验证：`0.2.38` bench-drive 候选已单车 OTA，设备 `get_info` 回报版本相符且 `otaState=CONFIRMED`；修复后的 `arm_bench_drive` 在 UTC 未就绪时用命令 requestId 建立会话，实机返回许可、坐标有效和 `feedbackGateMask=0`。一次 50 rpm 短时目标得到设备接受，但仅观察到左/右约 -1/6 rpm，随后设备重启；后续上电诊断读到 Node 2 状态字 `0x20D8`、右驱动故障对象 `0x2001=4`，运动门禁保持关闭。输出已下电，右电机 Hall 反馈及驱动器故障待现场排查。主机与台架构建通过不代表行驶验收；`dist/` 正式版仍为 `0.2.36`。

同日 wyse3040 接入 CAN 后被动抓包，两次上电均复现 Node 2 启动时 EMCY、状态字 `0x20D8`、左故障对象 `0x2000=1`（厂商定义为供电低压），约 0.9 秒后 EMCY 清除；这与较早一次读到的右侧 Hall 故障 `0x2001=4` 属不同时刻，不能合并为同一持续故障。平台两次均锁存 A-Box CAN Bus-Off，尽管后续 `canBusOffActive=false` 且 wyse3040 的 PCAN 保持 ERROR-ACTIVE、错误计数为零；总线电气根因尚未定位。未继续发送运动目标，输出已下电，下一步需测 Node 2 上电瞬间供电电压与 CAN 物理层。原始报文及哈希见 [现场证据](operations/evidence/2026-09-27-wyse-can-powerup/index.json)。

2026-09-28 Bus-Off 恢复候选：`0.2.42` 已单车 OTA，设备 `get_info` 回报 `CONFIRMED`；主机测试和台架构建通过。`get_can_diag.canBusOff` 保留本次启动的发生记录，常规状态的 `canBusOff` 表示当前恢复锁定，旧会话不会自动恢复。零速上电复验中 Node 2 故障短暂出现后清除，三个节点与实测反馈恢复，但 `get_info` 在运行 83490 ms 时报告 `boDiag=[83234,3]`，最近 Bus-Off 发生于查询前约 256 ms，说明 Bus-Off 在上电后继续重现，未满足稳定恢复条件；运动始终被拦截，末尾已下电。硬件/总线原因未定位，`0.2.42` 不能作为已验收正式版，`dist/` 仍为 `0.2.36`。原始上电记录与哈希见 [现场证据](operations/evidence/2026-09-28-busoff-recovery/index.json)。

同日用户移除 wyse3040 的终端电阻后，保持 `0.2.42` 不变再次零速上电，分别观察约 12 秒和 45 秒。上电前后 `boDiag` 的最近 Bus-Off 时间均为设备运行第 84414 ms，未记录新的 Bus-Off；当前 `canBusOff` 从锁定恢复为 false，`canBusOffActive=false`，节点与实测反馈在线、`powerReady=true`，双轮 0 rpm。每轮末尾均已下电，未发送运动命令。wyse3040 在此次被动抓包中没有收到帧，因此其接线或适配器状态仍需确认；现象支持终端改动后总线更稳定，但不能单凭两轮观察证明电气根因或运动验收。正式 `dist/` 仍为 `0.2.36`，原始记录见同一[证据索引](operations/evidence/2026-09-28-busoff-recovery/index.json)。

同日按用户报告改为 CAN1 与 wyse3040 两端启用终端电阻、驱动器关闭终端电阻，再次零速上电约 45 秒。上电前后 `boDiag=[84414,3]`，89 个上电状态样本均无当前 Bus-Off，末次 `canBusOffActive=false`；wyse3040 被动收到 7865 帧，接口保持 ERROR-ACTIVE，Bus-Off 与总线错误计数为 0。上电初期短暂驱动故障随后清除，末尾三个节点与实测反馈在线、双轮 0 rpm；已下电，未发送运动命令。此结果说明本次观察未复现 Bus-Off，不能替代全断电后的 CAN-H/CAN-L 约 60 Ω 实测，也未完成运动验收。原始记录与哈希见同一[证据索引](operations/evidence/2026-09-28-busoff-recovery/index.json)。

随后进行一次平台 10 rpm 架空远控：设备许可与目标应答成功，wyse3040 抓到 Node 2 对 `60FF=+10`、`68FF=-10` 的写入应答，但平台实测仅左/右 `0/1 rpm`，不能确认车轮转动。Node 2 随后出现故障状态字 `0x04BF`，CAN 报文中断、平台状态过期；A-Box 后续 `get_info` 运行时间归零，确认发生重启，重启原因仍未知。平台结束会话，测试脚本的下电命令超时。按用户要求，之后仅一次零速上电诊断并保持上电：Node 2 右故障对象 `0x2001=4`、状态字 `0x20D8`，`driveFault=true`、`powerReady=false`、双轮 `0/0 rpm`，CAN 无当前 Bus-Off；不再发送运动目标。当前阻拦是驱动故障和坐标无效，运动验收未通过，先排查右电机 Hall/反馈与供电及 A-Box 重启原因。原始平台、CAN、复查记录和哈希见同一[证据索引](operations/evidence/2026-09-28-busoff-recovery/index.json)。

同日电源保持开启时尝试转向前只读检查：最近状态仍为 `driveFault=true`、`powerReady=false`、`safetyReady=false`，且后续状态上报已过期；`get_bench_steering` 查询发出后超时，未拿到新诊断结果。wyse3040 仍能被动收到 CAN 帧且自身无 Bus-Off。因安全门禁与平台响应均不满足条件，未申请转向许可、未发转向目标，也未再下电。查询记录见同一[证据索引](operations/evidence/2026-09-28-busoff-recovery/index.json)。

之后再次核对设备在线与运行时间：`get_info` 于约 10:26:09 回报 `uptimeMs=179523`，推算 A-Box 又在约 10:23:10 重启。平台心跳在 10:23:02 至 10:23:27 间隔 25 秒，覆盖该重启时间；随后恢复约 5 秒一跳。复查时心跳距当前约 3.5 秒、底盘状态刚收到，设备在线且报告输出开启、双轮 0 rpm。`resetReason=0` 未给出重启原因；本次只读核验未切换电源或发送运动目标。心跳与运行时间原始证据见同一[证据索引](operations/evidence/2026-09-28-busoff-recovery/index.json)。

2026-09-28 速度候选：根据现场确认接插件松动已按紧、低速行走可见，源码将行走目标上限提高至 ±500 rpm，并将转向及回中共用的轮廓由 200/100/50 提高至 300/150/75 rpm；转向目标角仍为 ±30°。公共 MQTT V4 不变，产品业务 Profile 升至 1.1。主机测试、ARM 台架构建及候选哈希校验通过；`0.2.43` 已单车 OTA，`get_info` 回报版本相符、`otaState=CONFIRMED`，平台 OTA 任务完成。平台先部署 `0.4.155` 加入 500 rpm 档，但解析器只接受 Profile 1.0 Manifest，导致 1.1 心跳暂时无法归属、设备显示离线；随后 `0.4.156` 兼容 1.0/1.1 并部署，数据库记录 0.2.43 Manifest 已确认，心跳恢复在线。`get_bench_steering` 实机查询回报 `driveLimitRpm=500`，当时车辆输出关闭、运行坐标无效，未发运动目标。尚未复验 500 rpm 实测轮速、转向提速及回中时间；正式 `dist/` 仍为 `0.2.36`。

随后按当前约定将产品业务 Profile 保持为 1.0，直接在同一字段下使用设备能力上限。`0.2.44` 台架候选已通过主机测试和 ARM 构建，单车 OTA 任务完成，`get_info` 返回 `CONFIRMED`，平台已确认 Profile 1.0 Manifest、设备在线；`get_bench_steering` 返回 `driveLimitRpm=500`。车辆输出仍关闭，本次未发运动目标。此调整不改变行走与转向速度参数。

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

- 公共业务协议：[A-BOX 巡防车底盘业务协议 V4.0](protocols/A-BOX巡防车底盘业务协议-V4.0.md)；公共 MQTT V4 通用合同由该文档链接到 Platform 权威版本。
- 内部运维协议：[A-BOX 内部运维协议 V2.2](protocols/A-BOX内部运维协议V2.2.md)。
- 硬件与安全：[硬件与安全边界](hardware/硬件与安全边界.md)。
- 当前实现架构：[源码模块与依赖](implementation/源码模块与依赖.md)。
- 当前产品配置：[配置 V5 与掉电恢复](implementation/配置V5与掉电恢复.md)。
- 当前 MQTT 执行计划：[MQTT 双模式工作包 7-8 执行与验收](implementation/MQTT双模式工作包7-8执行与验收.md)。
- 当前车辆参数与标定：[车型参数与台架转向实现](operations/车型参数与台架转向实现.md)、[逐车标定与参数记录](operations/巡防车底盘逐车标定与参数记录.md)、[CANopen 执行约束](operations/CANopen执行约束.md)。
- Evidence 导航：[证据总索引](operations/evidence/README.md)。

完成的工作包、带日期的阶段结果和旧需求只在 `operations/history/` 查阅，不作为当前规范。
