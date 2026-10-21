# WP7.1 C 与 D1 本机测试机闭环

## 范围

- 对象为本机测试机，J-Link 69730334；不是现场设备。
- Boot 保持冻结 `abox-boot-2.3.0`，未修改 Boot 源码或产物。
- stable Config V5 为 plain；TLS candidate 只驻留 RAM。
- 测试 App 为开启 `PATROL_CHASSIS_MQTT_LAB_TEST` 的 Debug 取证构建，执行机构保持禁用。

## Reset 复核

复现前 fault mailbox 已包含历史记录 `reason=2`（stack overflow，PC/LR 为 0）。受控复现前后 mailbox SHA256 均为
`6D864E5E277114FB72AC544ED85E6F7B3DE5770DB8FF5FF956DB337969CCDF7A`，因此它不是本轮新增故障。

使用 HardFault、stack-overflow 和 Boot reset 断点重新执行旧 candidate 时，未命中 HardFault 或 stack hook。最初一次调试记录中的启动由 GDB 脚本自身 `monitor reset` 触发，不能归因于 TLS candidate。去除主动 reset 与高频 watchdog 断点后，旧实现以 invalid host 运行 90 秒无 MCU reset；它实际停在 runtime READY、client1/context2，由 MQTT Core 重试 `QMTOPEN`。

任务栈水位快照显示 Remote 4 KiB 栈剩余 2580 bytes；Watchdog、Diag、Power、Encoder、CANopen、CAN RX 和 Safety 均保留有效余量。本轮没有新的 HardFault、stack overflow、malloc failure、software reset、IWDG/PIN/POR 证据。

## 根因与修复

`mqtt_runtime.c` 在 TLS bind 完成后过早进入 `MQTT_RUNTIME_READY`，未等待 client1 MQTT connect/subscribe。因此 invalid host 不能制造 runtime failure，也不能满足 C1。

修复仅增加 TLS connect 验证阶段：bind 后开放 client1 安全门并运行 MQTT Core；只有连接并订阅完成才进入 READY，60 秒未完成则进入 FAILED，并重新关闭安全门、暂停 MQTT Core。没有 plaintext fallback、跨重启 candidate 锁或 Boot 改动。

## C 实机结果

- candidate：TLS、client1、context2、invalid host。
- 60 秒后：`MQTT_RUNTIME_FAILED`，`ready=0`，仍为 TLS/client1/context2。
- 控制状态：permit/session/enable/target RPM/requested RPM/steering target 均为 0。
- `g_cloud.reboot_pending=0`。
- 75 秒观察窗口内没有新的 `[BOOT2] evt=BOOT_START`，没有 MCU reset。
- 没有 client0 恢复路径；重启前保持 client1。
- Config V5 前后 SHA256 均为 `A1E93C70B13B43FBF149402EE03FA8AC5A1B1FCF6A8004F2301C210DDD978835`。

## D1 旧 session 拒绝

主动重启后 stable plain 恢复为 runtime READY/client0。使用原 `mqtt-lab-old-session` 发送 renew，Broker trace 记录设备在
`2026-09-22T08:45:02.908845Z` 发布实际响应：HTTP 语义码 `400`、`bench permit or motion limit rejected`、目标 RPM/转角均为 0。随后 GDB 状态确认 session id 为空，remote/session/enable 和全部目标仍为 0。

结论：链路恢复未恢复旧 session 或运动授权；必须重新取得 permit 并建立新 session。

## 验证

- 产品 Python：36/36 通过。
- Platform CTest：14/14 通过。
- 底盘 bench steering/config/drive 全部通过。
- clean ARM Release：RAM 61,432/65,536 bytes，剩余 4,104 bytes；App SHA256 `65134BADA3C27D1EEDAD7208293BBA86C501956D7CCD739B254AC480CF711487`。

本记录关闭 WP7.1 C 与 D1 软件/本机实机门禁，不代表真实车辆运动验收或 OTA 下发。
