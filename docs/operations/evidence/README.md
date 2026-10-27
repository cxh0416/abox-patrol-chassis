# Evidence 索引

本目录保存原始、不可改写的验证材料。本索引只提供导航；当前状态先看 [CURRENT](../../CURRENT.md)，当前操作步骤看 `docs/operations/`。

## MQTT

| 日期 / 阶段 | 状态 | 版本或 candidate | 入口 | 是否代表当前候选 |
|---|---|---|---|---|
| 2026-09-22 WP7.1 A/B | CLOSED / PASS | WP7.1 candidate | [index](2026-09-22-mqtt-wp71-a/index.json) | 历史关闭门禁 |
| 2026-09-22 WP7.1 C/D1 | CLOSED / PASS | WP7.1 candidate | [README](2026-09-22-wp71-c-d1/README.md) | 历史关闭门禁 |
| 2026-09-22 WP7.1 RTOS A/B | PASS | WP7.1 candidate | [README](2026-09-22-wp71-rtos-ab/README.md) | 历史关闭门禁 |
| 2026-09-22 WP7.2 | CLOSED / PASS | App SHA `5AE13A13…B5D4` | [summary](2026-09-22-mqtt-wp72/test-summary.md) / [index](2026-09-22-mqtt-wp72/index.json) | 历史关闭门禁 |
| 2026-09-23 WP7.3 | CLOSED / PASS | App SHA `DE38325C…7251` | [index](2026-09-23-mqtt-wp73/index.json) | 历史关闭门禁，非正式发布 |
| 2026-09-24 入网与恢复 | PASS | 正式 `0.2.36`，App SHA `B12298E9…D0B9` | [index](2026-09-24-enrollment-0236/index.json) | 当前底盘现场验收；原始敏感记录受控保存 |

## Steering / Calibration / CANopen

- 2026-10-27：`0.2.102` 将转向目标改为单槽最新目标提交：`0x607A`后完成本轮`0x3F/0x2F`再处理新目标，删除触发前回退覆盖，并限制两次触发起始间隔至少`100 ms`。候选与 Full/App/State A/B 烧录包见 [index](2026-10-27-target-commit-flash-20261027-112403-819758/index.json)；尚未烧录和实车验证。
- 2026-09-02：CAN 与控制器标定起点，见本目录对应 JSON。
- 2026-09-08：0.2.5 发布、行走与转向台架材料，见 `2026-09-08-*`。
- 2026-09-14：中位置零、停车链、转向限位、故障记录及 J-Link 刷写，优先从各目录的 `index.md` 或 `index.json` 进入。
- 2026-09-15：0.2.31 转向速度与自适应验证，见 [index](2026-09-15-031-steering-speed/index.md)。
- 2026-09-27：wyse3040 被动 CAN 抓包与两次上电故障复现，见 [index](2026-09-27-wyse-can-powerup/index.json)。
- 2026-09-28：Bus-Off 恢复候选单车 OTA 与零速上电复验，见 [index](2026-09-28-busoff-recovery/index.json)。
- 2026-10-25：`0.2.91` 在关键反馈接近 250 ms 时效前预留 SDO 优先级，并在单次反馈过期时先执行有界受控停车；候选构建、相关主机回归和烧录包校验通过，见 [index](2026-10-25-feedback-priority-flash-2026-10-25-172803-318591/index.json)；尚未烧录和实车验证。
- 2026-10-25：`0.2.90` 兼容绝对编码器请求回显与位置响应被 UART IDLE 分为两个数据块的情况，拼接最多两个分块后重新同步有效 Modbus 响应；编码器主机回归和 ARM Release 通过，见 [index](2026-10-25-encoder-chunk-reassembly-flash-2026-10-25-151438-155107/index.json)；尚未烧录和实车验证。
- 2026-10-25：`0.2.89` 将绝对编码器接收缓冲区扩大到 32 字节，并在请求回显或无效前缀后重新同步有效 Modbus 响应；编码器主机回归和 ARM Release 通过，见 [index](2026-10-25-encoder-echo-resync-flash-2026-10-25-144921-677136/index.json)；尚未烧录和实车验证。
- 2026-10-24：`0.2.88` 将 CAN2 三帧应答改为逐帧有界重试，修复新板只返回 `0x531/0x521` 而丢失 `0x532/0x533/0x522/0x523` 的问题；ARM Release、CAN2 栈、协议主机回归和镜像布局检查通过，见 [index](2026-10-24-can2-multiframe-reply-fix-2026-10-24-175251-657970/index.json)；尚未烧录和实车验证。
- 2026-10-24：`0.2.86` CAN2 本地中位标定候选、协议/脚本主机验证及 Full/State 烧录包，见 [index](2026-10-24-can2-center-calibration-2026-10-24-141359-455116/index.json)；尚未烧录和现场标定。
- 2026-10-24：CAN2 标定脚本首次运行超时定位为 CANalyst 通道选错；只读 SWD 确认板载 `0.2.86`，物理 CH1 的 `0x530` 查询正常，见 [index](2026-10-24-can2-calibration-channel-fix-2026-10-24-143034-166434/index.json)。
- 2026-10-24：CAN2 标定命令发现 motion `flags=0x0A / stopReason=13`；SWD 故障记录与 CAN1 只读复核定位到行走驱动器 `0x6061=1`，而固件要求 Profile Velocity 模式 `3`，见 [index](2026-10-24-drive-mode-fault-2026-10-24-143519-015205/index.json)。
- 2026-10-24：行走驱动器模式复查为 `0x6061=3 / 0x6060=3 / 0x6041=0x5010`，模式不匹配条件已解除；软件运动故障锁存仍未清除，见 [index](2026-10-24-drive-mode-recheck-2026-10-24-144313-542811/index.json)。
- 2026-10-24：CAN2 当前中位流程发现 `phy-remote` 仍持有共享控制会话；motion flags `0x27` 中故障位为 0，但控制会话位为 1，标定 START 尚未执行，见 [index](2026-10-24-can2-calibration-remote-session-2026-10-24-144632-575563/index.json)。
- 2026-10-24：`0.2.87` 增加 CAN2 `PREPARE` 上电前自动回中抑制，避免等待机械中位确认时使用旧标定将转向轮驱动到最大角；并增加独立 `reset-fault` 与活动会话阻断，见 [index](2026-10-24-can2-center-calibration-prepare-2026-10-24-145608-455268/index.json)；尚未烧录。
- 2026-10-24：CAN2 当前中位提交完成，Config V5 保存 `steeringCenterCount=-8122 / absoluteCenterCount=0`，MCU 复位后配置与编码器零点仍读回一致；脚本曾把终态 `TOKEN + COMPLETE` 误报为失败，现已修正，见 [index](2026-10-24-can2-center-calibration-success-2026-10-24-150307-285589/index.json)。
- 2026-10-24：完整重新上电后 CAN2 status、CAN1 直接 `0x6064` 和 SWD 运行状态复核一致；保存中位 `-8122`，驱动器实际 `0x6064=-8122`，编码器 `0`，无活动故障或控制会话，见 [index](2026-10-24-can2-center-powercycle-check-2026-10-24-151024-692476/index.json)。

这些记录证明对应日期、固件和测试条件下的结果，不自动代表当前候选。当前车辆参数和操作方法以 `docs/CURRENT.md` 引用的运维文档为准。

## Release / OTA / Archived Raw Data

- 2026-10-12 `0.2.54` 单车 OTA 试运行失败并回滚到 `0.2.45`：[原始索引](2026-10-12-054-ota-rollback/index.json)；get_log 仅取回旧版启动记录，具体故障原因未定位，无现场主机且未操作探针或重试。
- 2026-09-28 `0.2.45` 正式打包与单车 OTA：[记录](2026-09-28-0245-ota/index.md)；Boot `CONFIRMED`，运动回归待执行。
- 2026-09-08 正式发布核验：[0.2.5 release verification](2026-09-08-0.2.5-release-verification.json)，仅代表当时版本。
- 旧 `out/` 现场材料：[raw archive index](raw/2026-09-09-out.index.json)；压缩包正文保持不变。

新增 evidence 时优先给目录增加 `index.json`、`index.md` 或 README，并在本页增加一行；不得为导航目的改写原始日志、既有哈希或冻结证据正文。
