# 行走驱动模式不匹配导致故障锁存

日期：2026-10-24

## 现象

运行 CAN2 当前中位命令时，初始状态返回：

- motion `flags=10 (0x0A)`：已标定且运动故障已锁存；
- `stop_reason=13`：`CHASSIS_STOP_EXECUTION_FAULT`；
- 转向与行走节点在线，左右轮速和转向、行走目标均为 0；
- 标定会话尚未启动，`center_phase=0`。

## 只读故障快照

SWD 读取 `0.2.86` 的故障记录，结果保存在 `fault-record.json`：

- `source=1`、`reason=13`；
- 行走执行器 `drive_phase=BENCH_FAULT`、`drive_step=READ_MODE`、`drive_fault=10`；
- 转向执行器处于 `BENCH_STOPPING / STEER_STEP_STOP`，其自身 `bench_fault=0`；
- `last_abort=0x06020000` 是最近一次 SDO Abort，不是本次模式回读失败的直接错误码；
- `fault_storage=2`，表示本地故障记录保存状态为失败，但 RAM 快照仍有效。

## CAN1 只读复核

CANalyst-II 物理 CH2 接 A-BOX CAN1，仅发送三次 SDO upload，不写参数、不使能、不运动。Node 2 当前值保存在 `drive-mode-readonly.json`：

- `0x6061` 实际显示模式：`1`；
- `0x6060` 当前模式命令：`3`；
- `0x6041` 状态字：`0x0050`；
- 固件按速度目标 `0x60FF/0x68FF` 控制，要求实际 `0x6061=3`（Profile Velocity）。

因此 `drive_fault=10` 的原因是行走驱动器运行模式回读不匹配。若仅清除故障锁存而不修正驱动模式，下次行走执行器启动时仍会再次锁存。

## 处理建议

1. 在转向轮中位、整车具备急停的条件下，先确认后轴驱动器使用 CiA402 Profile Velocity 模式，并确保 `0x6061` 实际读回为 `3`。
2. 如果当前 `0x6061=1` 是上位机模式或使能条件造成的，先按驱动器厂商流程恢复模式 3；本记录未修改任何驱动器参数。
3. 模式确认后，再通过 CAN2 执行显式 `RESET_FAULT`，随后重新读取 motion flags。
4. 故障未清除前不得重试中位提交；`RESET_FAULT` 只清软件锁存，不代表模式配置或物理故障已经修复。
