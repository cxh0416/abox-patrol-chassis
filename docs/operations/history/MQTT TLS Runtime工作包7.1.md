# HISTORICAL / CLOSED：MQTT TLS Runtime 工作包 7.1

> WP7.1 A/B/C/D1 已关闭。当前阶段、冻结端点和后续门禁见 [CURRENT](../../CURRENT.md)；本文件只保留关闭过程和实现背景。

## 范围和配置基线

本工作包只接入底盘 MQTT 双模式运行时，不改变 `set_mqtt` 持久化流程，也不把 RAM candidate 写入 Flash。正式产品配置来源以 Config V5 为唯一基线。V3/V4 定义、样例和迁移测试仅作为历史资产保留，不为其增加新的运行时或回滚兼容设计。

产品配置与公共 Platform 的边界如下：

```text
VcuRemoteConfig_t V5
  -> PatrolMqttConfig_FromV5()
  -> ABoxMqttConfig
  -> MqttRuntime_Init()/MqttRuntime_ApplyRamCandidate()
  -> MQTT_Core_ApplyRuntimeConfig()
```

`PatrolMqttConfig_FromV5()` 映射 `mqtt_host`、`mqtt_port`、`mqtt_username`、`mqtt_password`、`mqtt_tls_enabled` 和 `mqtt_tls_profile_id`。公共 MQTT Core/runtime 不读取 V5 struct、Flash 地址、字段偏移或配置版本。

## 双模式调用链

明文配置不调用 TLS 模块：runtime 直接选择 client 0、解除 MQTT Core 安全门禁，由原 MQTT Core 完成网络注册、`QMTOPEN`、`QMTCONN`、3 个订阅及发布。

TLS 配置使用一套共享对象和公共模块：

```text
OtaService CA 精确回读校验
  -> ABoxEc800CommandAdapter（查询 CCLK）
  -> ABoxEc800Tls_Start(context=2, profile CA)
  -> QSSLCFG sslversion / seclevel=1 / sni / ignorelocaltime / cacert
  -> ABoxMqttTls_Start(client=1, context=2, enabled=true)
  -> QMTCFG ssl 绑定
  -> MQTT Core client 1 QMTOPEN/QMTCONN/订阅/发布
```

当前 V5 只接受公共 profile 0；无效 profile 会在映射或 runtime 入口被拒绝。TLS 任一步失败均进入 `MQTT_RUNTIME_FAILED`，MQTT Core 安全门禁保持关闭，不会发起 client 0 明文连接。

## RAM candidate 与控制状态安全（D1）

`MqttRuntime_ApplyRamCandidate()` 只复制 candidate 到运行时 RAM。candidate 合法且当前 runtime 为 READY 时，先调用 `RemoteService_RevokeForNetworkChange()`，再暂停 MQTT Core 并断开当前 client。撤销动作清除：

- remote permit、control session 和 session id；
- enable request；
- target/requested RPM 和 steering target；
- replay request/response 缓存。

链路恢复只恢复通信；旧 session 无法续期，必须重新取得授权并建立新 session。此入口在 7.1 没有接到正式 `set_mqtt`，也不写 Config V5。

## 自动验证证据

主机 runtime 测试通过公共 `abox_ec800_command_port`、`abox_ec800_tls`、`abox_mqtt_tls` 的真实实现和模拟 AT port 验证：

- plain 选择 client 0，AT 记录中没有 `QSSLCFG`/MQTT SSL binding；
- plain -> TLS 切换先触发运动撤销，再断开 client 0；
- TLS 使用 client 1、context 2、CA 校验等级 1，并完成 MQTT SSL binding；
- 注入 TLS 配置失败后进入 FAILED，AT 记录中没有 client 0 `QMTOPEN`/`QMTCONN` 回退；
- Config V5 映射和 network-change revoke 由 bench config/session 测试覆盖。

`tools/run_tests.ps1` 当前通过产品 35 项 Python 测试、Platform 14 项 CTest 以及底盘 bench 测试。ARM Release 链接通过。

这些是源代码、主机模拟 AT 和交叉构建证据，不等同于 EC800 或 Broker 实测。7.1 完成门禁为本地测试 ABox + EC800 + Broker 的 A/B/C，以及不要求执行机构动作的 D1。D1 只证明控制状态已撤销，不证明真实车辆已经停止。

真实车辆运动、CAN 输出和执行机构安全归入最终现场 D2；没有整车不阻塞 7.2/7.3 的软件开发。A/B/C/D1 原始记录必须在完成当阶段时写入 `docs/operations/evidence/` 并建立 SHA256 索引。

## Release RAM

同一 Release 工具链下：

| 项目 | 接入前 | 接入后 | 变化 |
|---|---:|---:|---:|
| `.data` | 520 B | 520 B | 0 B |
| `.bss` | 47,660 B | 47,324 B | -336 B |
| `.noinit` | 20 B | 20 B | 0 B |
| `.ota_work` | 692 B | 964 B | +272 B |
| heap + stack reservation | 12,288 B | 12,256 B | -32 B |
| 链接 RAM 总量 | 61,192 B | 61,432 B | +240 B |
| 剩余 RAM guard | 4,344 B | 4,104 B | -240 B |

为容纳公共 OTA context 增量并保持至少 4 KiB guard，诊断日志 snapshot 从 3584 B 缩减为 2048 B，newlib heap reservation 从 8192 B 调整为 8160 B；4 KiB task stack reservation 未降低。最终 guard 为 4,104 B。

## 进入 7.2 的门禁

代码结构、主机测试、消费者编译兼容、RAM guard 以及本机 ABox + EC800 + Broker 的 A/B/C/D1 证据均已满足。C 在 TLS connect 超时后保持 FAILED 且不回退 client0；D1 已取得旧 session renew 的设备拒绝响应。结构化证据见 [2026-09-22-wp71-c-d1](../evidence/2026-09-22-wp71-c-d1/README.md)。WP7.1 本机门禁已关闭，可进入 7.2；这不代表真实底盘执行机构或 OTA 现场验收。

后续 7.2、7.3、持久化/重启/OTA/CA 异常均在本地完成。最终现场阶段只保留 D2 真实运动安全和整车回归，详见 [MQTT 双模式工作包 7-8 执行与验收](../../implementation/MQTT双模式工作包7-8执行与验收.md)。

## 2026-09-22 App 启动回归结论

同板、同 Boot、同 Config 的正式 0.2.32 与 7.1 candidate A/B 调试证明 SysTick、FreeRTOS tick 和 PendSV 均已正常接管。原 candidate 的复位循环由 OTA Platform port 新增 `mqtt_client_index` 后，产品 adapter 漏接且增量目录混入旧对象导致的结构体布局错配和 HardFault 引起，不是 Boot 跳转问题。

产品 adapter 已使用 `MqttRuntime_ActiveClient()` 接入该回调，并通过 clean Release 构建、真实板 tick/CanRx timeout/watchdog `0x07` 复核及全套主机测试。Boot 未修改。结构化证据见 [2026-09-22-wp71-rtos-ab](../evidence/2026-09-22-wp71-rtos-ab/README.md)。该启动 blocker 已解除，可以继续 7.1 A/B/C/D1；7.1 本身仍需 MQTT/EC800/Broker 实机证据后才能标记完成。
