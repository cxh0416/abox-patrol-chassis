# 中位提交前仍存在 phy-remote 控制会话

日期：2026-10-24

CAN2 标定工具读取到的 motion flags 为 `39 (0x27)`：

- bit 0：车辆电源输出命令开启；
- bit 1：已标定；
- bit 2：当前运动条件就绪；
- bit 3：故障锁存为 0；
- bit 4：本地 CAN2 接管位为 0；
- bit 5：存在控制会话。

SWD 只读读取共享控制上下文，会话标识为 `phy-remote`，`session_active=1`、`enable_request=1`，目标转速和转角均为 0。故障锁存和待停机状态均为 0。这说明 T12 物理遥控仍持有底盘控制权，而不是 CAN2 标定会话。

此时不能输入 `YES` 继续提交；`CenterCalibration_Start()` 会因 `CENTER_GATE_CONTROL_SESSION_ACTIVE` 拒绝。应：

1. 在终端输入非 `YES` 或按 Enter 取消本次提示；
2. 将 T12 的 A 开关关闭，摇杆回中，并停止其他控制脚本；
3. 等待至少 1 秒，使 300 ms 物理遥控租约失效；
4. 运行 `status`，确认 motion flags 不再包含 bit 5；
5. 再次确认车轮机械中位后运行 `current`。

本次读取不改变控制状态，也未提交中位。
