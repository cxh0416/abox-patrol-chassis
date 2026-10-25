# 绝对编码器分段接收修复

日期：2026-10-25  
候选版本：`patrol-chassis-0.2.90`

## 问题

`0.2.89` 已允许在接收缓冲区中搜索有效 Modbus 响应，并将缓冲区扩大到 32 字节。
J-Link 实机复测仍观察到格式错误持续增长，说明 UART5 的请求回显和编码器响应
可能被 `HAL_UARTEx_ReceiveToIdle` 分成两个独立的 IDLE 数据块返回。

原逻辑每个轮询仅调用一次接收，因此只拿到第一块便进入解析；如果第一块是
8 字节请求回显，就会结束本次事务，真正的位置响应没有被接收。

## 修复

- 每次读取最多接收两个连续 IDLE 数据块。
- 两块数据追加到同一个 32 字节缓冲区。
- 每收到一块都重新搜索地址、功能码、长度和 CRC 均有效的响应。
- 已收到有效响应时立即停止，不增加固定等待。
- 保持原有错误分类、轮询周期和超时值。

## 验证

- ARM Release 构建通过。
- `chassis_safety_test` 通过。
- 使用现场真实报文验证：仅含 8 字节请求时解析失败；增加 9 字节有效位置响应后解析成功。
- CAN2 协议回归通过。
- CAN2 栈检查通过。
- Full、App、State A/B 和版本绑定检查通过。

## 烧录

1. `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
2. `BootState_A.bin` → `0x0803E800`
3. `BootState_B.bin` → `0x0803F000`

可选独立 App：

`Patrol_Vehicle_Chassis_App.bin` → `0x08008000`

禁止整片擦除，保留 `0x0803E000..0x0803E7FF` 和
`0x0803F800..0x0803FFFF`。

## 烧录后验证

通过 J-Link 观察 `g_motion.diagnostics`：

- `encoder_seen=1`
- 旋转编码器时 `encoder_count` 变化
- `encoder_ms` 持续更新
- 无格式、CRC、超时错误继续增长

本包尚未烧录和实车验证。
