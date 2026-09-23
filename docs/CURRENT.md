# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。发布事实以正式 manifest 为准；候选状态以对应构建物和 evidence 为准。历史过程不在此重复。

## Current Release / Candidate

| 项目 | 当前值 | 权威来源 |
|---|---|---|
| 最新正式版 | `patrol-chassis-0.2.32` | `dist/release.manifest.json` |
| 正式 App SHA256 | `5B9FE0E88D25ECFB34C278C6594974944257FA305CBD250F28539E048B2DFEE3` | 正式 manifest |
| 当前开发版本 | `patrol-chassis-0.2.32`，WP7.2 checkpoint candidate，非正式发布 | 根目录 `VERSION` 与下方 checkpoint |
| WP7.2 最新实测 Debug App SHA256 | `5AE13A1367345515245F9D2C694710BA3140DD18F9DEC3FFF81BE3ABD471B5D4` | `out/logs/wp72-success-app.bin` 与 WP7.2 index；测试构建，非正式版 |
| 仓库 Platform gitlink | `26a8ab3687336a39f30e578b713b43edbb3488b0` | 当前 checkpoint gitlink；未 push |
| WP7.2 checkpoint App SHA256 | `6EE9E4188E001B8581CA4198F9A4B7D1D0D91F132273CA0955325C1295BB450E` | clean ARM Release candidate；非 `dist/` 正式版 |
| 冻结 Boot | `abox-boot-2.3.0`，SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63` | 正式 manifest；Boot `0x08000000` |

候选与正式版版本字符串相同不表示候选已正式发布。正式状态只能由 `dist/release.manifest.json` 及其固件哈希确认。

## Current Phase

```text
WP7.1 CLOSED          A/B/C/D1 passed
WP7.2 CLOSED
WP7.3 NOT STARTED
D2 REAL VEHICLE NOT STARTED
```

## Current Checkpoint

```text
Patrol checkpoint commit = a8b952663a8e5c109a5c30da889d3cef465d0ad5
Platform commit = 26a8ab3687336a39f30e578b713b43edbb3488b0
App SHA256 = 6EE9E4188E001B8581CA4198F9A4B7D1D0D91F132273CA0955325C1295BB450E
```

该 App 是可重复构建 checkpoint candidate，不是 `dist/` 正式 release，未执行 OTA 下发。

进入 WP7.3 前已按 ABox Platform、产品仓通用能力和 Patrol 产品能力完成命名收口。当前产品 Flash Config 只接受 V5；V3/V4 仅属于开发历史，不进入正式运行路径。除此项明确的兼容策略收紧外，无功能、公共协议或 Flash 布局变化。公共 MQTT V4 与 Config V4 是不同版本概念，MQTT V4 未修改。WP7.2 仍为 CLOSED，WP7.3 仍未开始。

WP7.2 最新实机状态：

- plain → plain、plain → TLS、TLS → TLS、TLS → plain：2026-09-23 关闭注入后的实机复测均 `COMMITTED`
- TLS candidate failure → restore plain：PASS
- subscribe timeout、business proof timeout：实机 `TIMEOUT → RESTORING → plain READY`，reason=2
- cancel：实机 `VERIFYING → RESTORING → plain READY`，reason=3
- restore failure：实机 `RESTORE_FAILED`，runtime `FAILED`，MQTT 安全门为 0；未见隐式 plain fallback
- 成功矩阵之后重新物理读取 `0x0803F800`，`config-after-wp72.bin` SHA256 为 `A1E93C70B13B43FBF149402EE03FA8AC5A1B1FCF6A8004F2301C210DDD978835`，与基线一致；Boot 物理回读也与冻结产物一致

2026-09-23 补采：COM3 为 FTDI USB Serial Port，按 UART5 `19200 8N1` 采集。原始串口记录包含 TLS trial 的 `QMTOPEN/QMTCONN`、PREPARED/CONNECTED/SUBSCRIBED、proof 和 commit；EMQX 原生 trace 包含同一 clientId 的 CONNECT/CONNACK 与 9 组 SUBSCRIBE/SUBACK。仅为取证复现一次 TLS trial，未重跑故障矩阵。最终产品/Platform/bench 全量回归通过；clean ARM Release 通过，故障注入与实验命令均关闭，RAM guard 为 4,104 B。WP7.2 关闭，WP7.3 尚未开始；本次验证不改变 `dist/` 的正式版身份。

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
