# Boot State 修复记录

现场现象：`0.2.83` App 已烧录，但 Boot 不跳转，4G、CAN 控制和整车运动均未启动。

根因：App 区为 154,472 字节、CRC32 `D51C4A05`、SHA256 `86C79DF4F076A9A52BE67C3877A9AA56643199E5CF91573332D32BFB329ED6B7`；旧 State A 仍记录 `0.2.82` 的 154,264 字节和 CRC32 `F4F02E02`。Boot 仅在两值匹配时跳转。

修复：使用当前 App 生成匹配的 State A/B，只写 `0x0803E800` 和 `0x0803F000`。State A 记录 CRC `5728D5AB`，State B 为擦除态。未改 App、Boot、`0x0803E000` 恢复页或 `0x0803F800` Config V5。

验证：复位后连续五次采样 `VTOR=0x08008000`，App tick 持续增长，CFSR/HFSR 为 0。App RAM 日志确认 MQTT/TLS `READY`，心跳、状态和校时均已发布；当前 `faultLatched=false`，软件输出关闭，处于待机。Config 页修复前后哈希一致。

本记录证明固件启动和 4G 连接恢复，不证明整车真实运动或机械安全验收。
