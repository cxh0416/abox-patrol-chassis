# CAN2 中位完整上电后的只读核查

日期：2026-10-24  
设备固件：`patrol-chassis-0.2.87`

## 结论

Config V5 保持 `steeringCenterCount=-8122`、`absoluteCenterCount=0`、`calibrated=true`。重新上电后：

- CAN2 status 中 `saved_center=-8122`；
- 独立编码器 `encoder_raw=0`；
- CAN1 直接读取 Node 1 `0x6064=-8122`，与保存中位一致；
- Node 2 `0x6061=3`，行走驱动模式匹配；
- 控制上下文无会话、无使能请求、无故障锁存或待停状态；
- 左右轮速均为 0。

CAN2 status 中 `driver_raw=0` 是 power output 关闭时诊断缓存没有继续刷新的占位值；同一时刻通过 CAN1 直接读取 `0x6064` 得到真实值 `-8122`。

## J-Link 检查

只读 SWD 确认运行 App 为 `0.2.87`，启动日志为：

```text
I CFG loaded vid=PATROL-CHASSIS-000
I PWR vehicle output commanded=0 result=0
I REMOTE initialized calibrated=1; outputs gated
I BOOT version=patrol-chassis-0.2.87 app=0x08008000
```

`fault_storage=1`、`fault_dirty=0`。`last_fault` 是重启前保留的历史方向保护记录 `sequence=8 / reason=12`，但当前 `fault_latched=0`，不是活动故障。

`stop_reason=3` 对应断电状态下的反馈过期，属于预期；重新上电并刷新反馈后不应作为故障。

## 边界

本次确认了配置值在重新上电后保留、驱动器当前位置与保存中位一致、编码器读回零，以及无活动故障。尚未进行真实转向动作、行走、自动回中运动或机械角度独立测量。
