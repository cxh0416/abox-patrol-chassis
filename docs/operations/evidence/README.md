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

- 2026-09-02：CAN 与控制器标定起点，见本目录对应 JSON。
- 2026-09-08：0.2.5 发布、行走与转向台架材料，见 `2026-09-08-*`。
- 2026-09-14：中位置零、停车链、转向限位、故障记录及 J-Link 刷写，优先从各目录的 `index.md` 或 `index.json` 进入。
- 2026-09-15：0.2.31 转向速度与自适应验证，见 [index](2026-09-15-031-steering-speed/index.md)。
- 2026-09-27：wyse3040 被动 CAN 抓包与两次上电故障复现，见 [index](2026-09-27-wyse-can-powerup/index.json)。
- 2026-09-28：Bus-Off 恢复候选单车 OTA 与零速上电复验，见 [index](2026-09-28-busoff-recovery/index.json)。

这些记录证明对应日期、固件和测试条件下的结果，不自动代表当前候选。当前车辆参数和操作方法以 `docs/CURRENT.md` 引用的运维文档为准。

## Release / OTA / Archived Raw Data

- 2026-09-28 `0.2.45` 正式打包与单车 OTA：[记录](2026-09-28-0245-ota/index.md)；Boot `CONFIRMED`，运动回归待执行。
- 2026-09-08 正式发布核验：[0.2.5 release verification](2026-09-08-0.2.5-release-verification.json)，仅代表当时版本。
- 旧 `out/` 现场材料：[raw archive index](raw/2026-09-09-out.index.json)；压缩包正文保持不变。

新增 evidence 时优先给目录增加 `index.json`、`index.md` 或 README，并在本页增加一行；不得为导航目的改写原始日志、既有哈希或冻结证据正文。
