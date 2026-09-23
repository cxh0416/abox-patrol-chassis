# MQTT WP7.2 当前验收摘要

对象：本机测试机（J-Link 69730334），不是现场设备。

## 2026-09-22 已通过（前次候选）

- 公共 `ABoxMqttRuntime` plain client0 与 TLS client1/context2 编排、connect+subscribe READY gate。
- 公共 `ABoxMqttTrial` generation/session、stale proof、timeout、cancel、restore timeout/failure 主机矩阵。
- 产品 Python 36/36、Platform CTest 14/14、底盘三组 bench 测试。
- clean ARM Release 构建；RAM 61,432/65,536，guard 4,104 bytes。
- 实机 plain -> plain：trial session 1 到 `COMMITTED`。
- 实机 plain -> TLS：使用 `zxwl.ntiov.com:19688`，旧 plain 连接返回 `RAM_CANDIDATE_ACCEPTED` 后断开；TLS candidate 重新 CONNECT、完成 3 个订阅并产生新发布 proof，运行态回读为 `tls=true/client=1/context=2/trialState=8`。
- 实机 TLS -> plain：使用 `36.153.162.171:19683`，candidate 重新 CONNECT、完成 3 个订阅并产生新发布 proof，运行态回读为 `tls=false/client=0/context=0/trialState=8`。
- 实机 TLS -> TLS：active 和 candidate 均使用 `zxwl.ntiov.com:19688`，candidate 重新 CONNECT、完成 3 个订阅并产生新发布 proof，运行态回读为 `tls=true/client=1/context=2/trialState=8`。
- 实机失败恢复：TLS candidate 失败后进入 `RESTORING`，修正已断开 client 的 best-effort cleanup 后，原 plain active 重新连接、完成 3 个订阅并取得新发布 proof，trial 进入 `FAILED`（恢复完成后的预期终态）。
- Config V5 试验前后 SHA256 均为 `A1E93C70B13B43FBF149402EE03FA8AC5A1B1FCF6A8004F2301C210DDD978835`。

## 固定端点

```text
TLS test public endpoint:
zxwl.ntiov.com:19688

Backend EMQX TLS listener:
192.168.4.196:8883
```

`19688` 是设备公网 endpoint；`8883` 仅为 NAT 后的 EMQX 内网 listener。此前对公网 `8883`、`19888`、`19684` 的尝试不计入 WP7.2 TLS trial 结论。

## 原始 Broker 证据

- `plain-to-tls-19688-broker.jsonl`：SHA256 `8A765352C6F12D138A96A237AF0B3C5CD64BB88B3E9972E54A93142328F3C1F5`
- `tls-to-plain-broker.jsonl`：SHA256 `909214D845F58395A0CDEE0507781354A3CC257B837780A2267B8146977FF295`
- `tls-to-tls-broker.jsonl`：SHA256 `4FC45299E2B7587F159CDAA475672642FB30521A01FC56FE5015E6A5C2DFE05C`

每份 trace 均保留 request、旧连接 accepted、DISCONNECT、新 CONNECT、3 个 SUBSCRIBE/SUBACK、candidate 后新发布及运行态回读。TLS profile 0 由 candidate 请求和 accepted 响应证明；client1/context2 由运行态回读证明。Broker trace 无法单独证明 modem context/profile 的 AT 配置细节，相关原始 AT 证据仍引用 WP7.1 B。

## 2026-09-23 当前代码实测

- 公共 runtime 在 prepare 完成、MQTT CONNECT 成功、全部订阅成功时分别发出真实事件；产品 Trial 不再一次性补发中间状态。candidate proof 仅接受本次连接后的产品心跳发布完成。
- 关闭注入的 Debug App SHA256 为 `5AE13A1367345515245F9D2C694710BA3140DD18F9DEC3FFF81BE3ABD471B5D4`。plain→plain、plain→TLS、TLS→TLS、TLS→plain 四项实机复测均为 `COMMITTED`；运行态回读见 `wp72-2026-09-23-broker-observer.jsonl` 中的 `wp72-success-*` 请求。
- `STALL_SUBSCRIBE`：J-Link 实测 runtime `CONNECTED=9`、Trial `SUBSCRIBING=4`，随后 Trial `FAILED=9/reason=TIMEOUT(2)`，原 plain/client0 恢复。第一次注入由 runtime 订阅超时先触发并记为 ERROR；仅测试构建的订阅超时延长后重新取得 Trial TIMEOUT 证据。
- `DROP_PROOF`：TLS candidate 为 `READY/client1/context2`、Trial `VERIFYING=5`，Broker observer 收到 candidate 心跳；Trial 未接受 proof，到期后 `FAILED/reason=TIMEOUT(2)` 并恢复 plain/client0。
- `cancel`：在 `VERIFYING` 收到测试构建专用 cancel，确认响应后断开 candidate；J-Link 实测 `FAILED/reason=CANCELLED(3)`，原 plain/client0 恢复。
- `FORCE_RESTORE_FAIL`：在 proof timeout 后阻断 active 恢复；J-Link 实测 runtime `FAILED=11`、Trial `RESTORE_FAILED=10/reason=TIMEOUT(2)`、MQTT security-ready 为 0，之后未见自动 plain 心跳。
- 四类成功矩阵后重新 J-Link 物理读取 `0x0803F800`，新文件 `config-after-wp72.bin` SHA256 为 `A1E93C70B13B43FBF149402EE03FA8AC5A1B1FCF6A8004F2301C210DDD978835`，与固定基线一致。Boot 物理回读 SHA256 与冻结 Boot 均为 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63`。
- 三种注入构建及关闭注入的 Debug 构建 RAM 均为 61,432/65,536 B，guard 4,104 B。直接相关的产品主机测试通过。

## 2026-09-23 补证及最终门禁

- COM3 是 FTDI USB Serial Port，UART5 为 `19200 8N1`。`wp72-com3-20260923-raw.log` 保存一次开机连接；`wp72-com3-trial-20260923-raw.log` 保存一次仅为取证执行的 TLS trial，其中有 `QMTOPEN client=1 result=0`、`QMTCONN client=1 result=0 ret=0`、真实阶段转换、心跳 proof 和 commit。先前“COM3 未枚举”的判断错误，源于只查询了 WMI 列表；COM11 的 0 字节文件保留作为原始记录，不充当 AT 证据。
- `wp72-emqx-session-trace.zip` 是 EMQX 原生 clientId trace 下载包；其中有同一设备的 CONNECT/CONNACK 3 组、SUBSCRIBE/SUBACK 9 组。它与 COM3 记录共同证明本次连接和订阅，先前的 Broker observer 仍用于 request/response 与心跳证明。
- 完整产品测试、Platform CTest 14/14 及 bench 测试通过。首次全量运行发现旧调度器测试桩缺少 Trial proof 符号，仅补测试桩后受影响测试及完整门禁通过；业务代码保持冻结。
- clean ARM Release 通过，`PATROL_MQTT_TRIAL_FAULT_INJECTION=OFF`、`PATROL_CHASSIS_MQTT_LAB_TEST=OFF`，RAM 61,432/65,536 B，guard 4,104 B。构建 App SHA256 为 `37F182C8D1772A7A9EB4AEB67B8BAA20723FE5779D5203EF1CCEA05A75C2CDF9`。

WP7.2 CLOSED。WP7.3 尚未开始；本轮没有替换正式 `dist/`。
