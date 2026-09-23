# MQTT 双模式工作包 7-8 执行与验收

## 固定边界

- 正式产品只维护并接受 Config V5；Config V3/V4 仅属于开发历史，不保留运行时解析或迁移测试。
- 公共运行时统一使用 `ABoxMqttConfig`，显式支持 plain 与 TLS 双向切换。
- `tls_enabled=true` 的任何失败都必须 fail closed，禁止自动尝试明文。
- Platform/runtime 不依赖产品 Flash layout。
- 构建、主机测试、模拟 AT、ABox/EC800 实测和真实车辆运动验收是不同证据等级，不得互相替代。

## 测试与验证成本控制

- 开发阶段按语法/编译检查、直接相关单元测试、相关模块测试的顺序执行最小测试集；同一代码状态下已通过且覆盖范围未受本次改动影响的测试不重复运行。
- 相关修改和故障注入先集中完成，再统一 ARM 构建、烧录和连续执行实机矩阵；不得每改一点就重跑全量测试。
- 阶段候选完成后运行一次完整产品、Platform CTest 和 bench 回归；正式候选冻结前运行一次 clean ARM Release 和最终门禁。只有前次结果因代码或环境变化失效，或失败需要定位时才重复高成本测试，并记录原因。
- 成功测试只保留结果摘要；失败时只提取失败用例和定位所需日志。阶段测试基本完成后统一整理 `docs/CURRENT.md`、本执行文档和 evidence 索引；原始 evidence 不修改，不为每个测试结果新增 summary 文档。
- 补充取证只运行产生缺失证据所必需的场景；已通过且与取证无关的实机结果继续有效，不重跑。WP7.2 代码冻结后不为取证修改业务实现。

## 阶段和门禁

### 阶段一：7.1 本地实机通信

设备范围为本地测试 ABox、EC800 和 Broker，不连接真实底盘执行机构。

| 项目 | 必须保存的证据 |
|---|---|
| A plain | client 0 `QMTOPEN/QMTCONN`、3 个订阅、heartbeat、status/report、PUBACK；确认无 `QSSLCFG` 和 SSL binding |
| B TLS RAM candidate | CA provision/readback、context 2、profile 0、`sslversion`、`seclevel=1`、`sni`、`ignorelocaltime`、`cacert`、SSL binding、client 1 连接/订阅/发布，以及 Broker 侧 CONNECT/订阅/发布 |
| C TLS failure | FAILED 状态、无 client 0 回退、V5 和 active/stable 未改变 |
| D1 software revoke | permit/session/session id/enable/drive/steering/replay 全部清除，旧 session renew 被拒绝 |

A/B/C/D1 全部通过并归档后标记 7.1 完成，进入 7.2。D1 不代表真实车辆已经停止。

### 阶段二：7.2 公共 trial 闭环

接入公共 `abox_mqtt_trial`。candidate 原子包含 host、port、username、password、`tls_enabled`、`tls_profile_id`。

WP7.2 的 TLS 测试端点固定如下，不得根据 Broker listener 端口推测公网端口：

```text
TLS test public endpoint:
zxwl.ntiov.com:19688

Backend EMQX TLS listener:
192.168.4.196:8883
```

`19688` 是设备经公网访问的 MQTT TLS endpoint，NAT 后端才是 `192.168.4.196:8883`。底盘设备不得直接把公网目标设置成 `:8883`；`19888` 和 `19684` 也不是 WP7.2 已验证端点。

状态链为：

```text
active -> ACCEPTED -> PREPARING -> CONNECTING -> SUBSCRIBING
       -> VERIFYING -> COMMITTING -> COMMITTED
failure -> RESTORING -> active
```

本地覆盖 plain→TLS、TLS→plain、TLS→TLS、plain→plain。每类都验证 connect、全部订阅、business proof、失败恢复、stale session/proof、timeout/cancel 和 restore failure。7.2 不写 Config V5。

2026-09-23 WP7.2 CLOSED。真实阶段事件、四类故障注入、四类成功复测与矩阵后 Config 物理回读已完成；COM3 原始串口记录及 EMQX CONNECT/SUBACK 原生 trace 已补齐。最终产品/Platform/bench 全量回归及 clean ARM Release 通过。证据以 `docs/operations/evidence/2026-09-22-mqtt-wp72/index.json` 为准。

### 阶段三：7.3 set_mqtt 与 V5 commit

`set_mqtt` 必须先在旧连接回复 accepted，再撤销控制状态并启动 trial；只有 candidate 已连接、完成订阅和 business proof 后才保存 Config V5。Flash readback 成功后发出 SAVED 并 commit。任何失败不保存 candidate，并恢复原 active。

请求完整包含 vid、host、port、username、password、必填 `tlsEnabled` 和 `tlsProfileId`。`get_info` 返回 `mqttHost`、`mqttPort`、`mqttTls` 和 `mqttTlsProfileId`。

2026-09-23 本地软件候选：产品层将完整请求映射到 Config V5 与 `ABoxMqttConfig`；旧连接的 QoS1 受理响应完成后才调用 Trial Accepted。Trial prepare 时更新产品 VID、ClientId 和当前三条订阅 Topic；restore 时恢复已持久化 VID 及 Topics。只有 candidate heartbeat proof 后的 COMMITTING 才通过 `PatrolConfig_Save()` 写入并回读 Config V5；该函数失败时尝试恢复旧配置页，恢复写入失败则保持网络安全门关闭。最终响应复用 requestId，发送完成后覆盖 202 幂等缓存。已持久化配置仍是 `get_info` 的来源。

2026-09-23 WP7.3 CLOSED。本地测试、Platform CTest 14/14、bench、clean ARM Release 与 4 KiB RAM 门禁通过。实机 plain→TLS、TLS→plain 均先旧连接 202，后新连接 200；J-Link Config V5 回读和重启恢复相同模式通过。失败 candidate 与失败 VID candidate 均返回最终 500、恢复旧 ClientId/Topics，Config 物理哈希不变；成功 VID 切换的新 ClientId、三条订阅及 heartbeat/report Topic 已见 Broker 原生 trace。重复 requestId 重放最终 500 而非 202。OTA_Info 非 MQTT 字段、同页故障日志以及 Boot State A/B 在 `set_mqtt` 前后保持不变。实测完成后原 App、Boot State A/B 和 Config 已物理回读确认逐字节恢复。证据见 [WP7.3 index](../operations/evidence/2026-09-23-mqtt-wp73/index.json)；此结论不代表阶段五 D2 真实车辆验收或正式发布。

### 阶段四：本地持久化、掉电、OTA 和 CA 异常

- TLS/plain candidate 成功保存后正常复位，必须直接恢复相同安全模式。
- Flash 保存失败不得 commit，必须恢复 active。
- Config V5 主写入中断、主写完成但 recovery page 未清等掉电点必须读取 recovery page、Config page 和 fault journal，并保存原始 Flash 与哈希。
- 分别完成 plain MQTT + HTTPS OTA、TLS MQTT + HTTPS OTA；MQTT 固定 context 2，HTTPS/OTA 固定 context 1，验证暂停/恢复 client index、V5 跨 OTA 保持和重启恢复模式。
- 验证 CA 正常、缺失、损坏、恢复；失败时保持 TLS fail closed。
- 若执行同版本 OTA，继续验证平台 `progress_observed` 门禁。

### 阶段五：7.4 现场 D2

前四阶段完成后才带候选去真实底盘。建立真实运动 session 后触发 plain/TLS 切换、candidate 失败/恢复和网络断开/恢复，联合观察 CAN、驱动、转向、session、MQTT 和网络状态。

D2 必须证明车辆实际停止、CAN/驱动进入安全状态、旧动作不恢复、旧 session 失效、重新授权前不能运动、目标无残留，并且 TLS/网络故障期间持续保持停止。

## 证据归档规则

每个阶段完成时立即建立一个日期化目录：

```text
docs/operations/evidence/YYYY-MM-DD-mqtt-wp7x/
  index.json
  device-at.log
  broker.log
  config-before.bin
  config-after.bin
  flash-readback.bin
  test-summary.json
```

只保存当阶段实际产生的文件；不得创建空日志或伪造未执行项目。`index.json` 至少记录时间、设备标识、固件候选 SHA256、Platform commit、测试配置、每个原始文件 SHA256、各验收项 PASS/FAIL/NOT_RUN 和证据路径。涉及 Config/掉电时同时记录配置页、recovery page 和 fault journal 的地址、长度和哈希。

## 发布门禁

正式发布结论必须同时满足：7.1 A/B/C/D1、7.2 四类 trial、7.3 candidate/V5 commit、V5 硬件掉电恢复、plain/TLS OTA、CA fail closed、RAM guard 不低于 4 KiB，以及现场 D2。阶段五之前允许完成所有软件开发和本地实机验收，但不得将其表述为真实车辆安全验收。
