# CAN2 标定脚本通道诊断

日期：2026-10-24

现象：烧录 `patrol-chassis-0.2.86` 后运行默认 `run_center_calibration.ps1 current`，等待 `0x531/0x532/0x533` 超时；整车重启后现象相同。

只读 SWD 结果：

- App SHA256 为 `84590b4bbe63f16a917409d611137669c712c6ea68e21c4de3c2247bd1043968`，与 `0.2.86` 候选完全一致。
- `VTOR=0x08008000`，CPU 已进入 App，不是 Boot State 阻止启动。
- 本次检查未复位、未暂停、未写 Flash。

本地 CAN 结果：

- 该车实际接线为分析仪物理 CH1 接 A-BOX CAN2、物理 CH2 接 A-BOX CAN1。
- 原工具默认把物理 CH2 当作 CAN2，因此请求被发到 CAN1，`status-wrong-ch2.log` 超时。
- 在物理 CH1 发送只读 `0x530 QUERY` 后正常收到三帧响应，`status-ch1.jsonl` 中 result=OK。
- 当前读取值为驱动器中位候选 `-8122`、独立编码器 `1642`、Config V5 已保存旧中位 `-83815`。这只是查询结果，尚未提交新标定。

修复：包装脚本和 Python 脚本的默认通道改为当前接线的物理 CH1；如改用物理 CH2，显式设置 `-Channel 2` 或 `--channel 1`。
