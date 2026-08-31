# MQTT V3 远控协议

公共 Topic 根为 `/zxwl/abox/{vid}`，`deviceType` 固定为 `patrol_vehicle_chassis`。本文不定义厂商 CANopen。

控制请求使用 `/zxwl/abox/{vid}/chassis/control`：

```json
{"version":"3.0","timestamp":1788134400000,"data":{"requestId":"req-1","deviceType":"patrol_vehicle_chassis","command":"chassis_control","params":{"action":"acquire","sessionId":"session-1","seq":1,"leaseMs":1000,"driveRpm":50,"steeringDeg":1}}}
```

`action` 只允许 `acquire`、`renew`、`stop`；`seq` 在会话内严格递增；设备租约上限 1000 ms。`stop` 必须显式清零驱动与转向目标并释放会话。

控制响应发布到 `/zxwl/abox/{vid}/chassis/control/resp`，只说明命令是否被设备安全仲裁接受。状态发布到 `/zxwl/abox/{vid}/chassis/status`，至少包含目标转速/转角、左右实测转速、驱动器转角、独立绝对角、两个节点在线、标定、电源、`safetyReady` 和安全停止原因。MQTT ACK 不代表车辆已经运动。

