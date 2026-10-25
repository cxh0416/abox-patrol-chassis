# CAN2 当前中位提交成功与脚本收尾误报

日期：2026-10-24  
设备固件：`patrol-chassis-0.2.87`

## 操作结果

操作者确认转向轮处于机械中位后执行 CAN2 `current`。设备完成：

- 当前驱动器 `0x6064` 捕获值：`-8122`；
- 编码器置零并连续三次读取为 `0`；
- Config V5 保存并再次读取 `steeringCenterCount=-8122`、`absoluteCenterCount=0`、`calibrated=true`；
- 随后 MCU 重启，`status-after-mcu-reset.json` 仍读取保存中位 `-8122`，证明 Config V5 加载成功；
- 当前 power output 关闭、无故障、无控制会话；`stop_reason=3` 来自断电后的反馈时效，不是执行故障。

## 脚本误报

首次完成时终端显示：

```text
ERROR: Calibration query failed: result=TOKEN, phase=COMPLETE, reason=NONE
```

这是电脑端脚本的状态判断错误，不是设备提交失败。标定完成时固件按设计终止本地会话并清除会话令牌，而下一次轮询仍携带旧令牌，因此 response 为 `TOKEN`，同时 phase 已为 `COMPLETE`。设备随后读取到：

```text
phase=COMPLETE
saved_center=-8122
calibrated=true
```

脚本已修正为：在等待 `COMPLETE` 时，`TOKEN + phase=COMPLETE` 是正常终态；其他令牌错误仍按失败处理。增加回归后 CAN2 工具测试通过。

## 验证边界

本记录证明 Config V5 在 MCU 复位后重新加载，且保存值与编码器零点一致。尚未进行整车动力完全断开后的编码器本体零点保持验收，也未进行转向、行走和自动回中实车动作验收。
