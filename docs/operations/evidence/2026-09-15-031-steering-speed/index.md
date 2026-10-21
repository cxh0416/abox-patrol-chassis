# 0.2.31 转向速度轮廓发布与 Wyse 刷写证据

本次通过 `server47:6008` 的 FRP 转发连接 `wyse3040`，使用 J-Link 69730334、STM32F105RC、SWD 4000 kHz 更新 App。刷写前平台返回设备离线，Wyse `can0` 以 500 kbit/s 被动监听 3 秒未收到 CAN 帧；因此没有发送运动或使能命令，也不能据此声称设备物理断电或现场运动安全已经验收。

正式 App 为 `patrol-chassis-0.2.31`，109128 bytes，SHA256 `C8AB6FAEAB6B3C77FAEABC39FD2A78F443F3EE4E76B9016541D6DCCF00760A34`。J-Link 写入后读回 App 与正式包逐字节一致。Boot State A 从有效序号 20、版本 0.2.30 更新为有效序号 21、版本 0.2.31；Boot State B 保持序号 19、版本 0.2.30。Boot、配置页和 Boot State B 的读回字节均与刷写前一致。

平台在复位后的首次查询仍返回设备离线；稍后再次查询成功，设备报告 `fwVersion=patrol-chassis-0.2.31`、`mqttState=READY`、`otaState=CONFIRMED`、`otaPhase=IDLE`、`otaTargetVersion=patrol-chassis-0.2.31`、`bootDescriptorValid=true`、`trialFailCount=0`、`powerOutputOn=false`。这证明 App 已运行并完成平台上线，不是停留在 Boot；逐车 VID、ICCID 和 OTA 配置也可读取。随后同批 `get_bench_steering` 查询超时，且当时左右驱动心跳均为 false、CAN 接收计数为 0，因此本记录仍不证明真实转向、行走或完整现场验收通过。

## 归档文件

- `out/logs/wyse-031-backup.log` -> `wyse-031-backup.log`, SHA256 `0C49DCE5B8D43FE7295F59D96AA4A9EA5DBF477C55E05DAC06FE7BD9812C11CD`
- `out/logs/wyse-031-flash.log` -> `wyse-031-flash.log`, SHA256 `4253ED179EC66A7EBD591BEBB9616555FC229F23F36A7059D306036F74F1B4F5`
- `out/logs/flash031-result.json` -> `flash031-result.json`, SHA256 `7D31C327E2D40127D8A7B0AF513A60F33D02D772708E26C5D59E52653C8C2E5F`
- `out/logs/preflash031-result.log` -> `preflash031-result.log`, SHA256 `797047E54E0506C12762A5C20832B5E5A8A34EDA1933B421C2C4B9961F68845B`
- `out/logs/postflash031-result.log` -> `postflash031-result.log`, SHA256 `D2E9C8EA624FECCF95549FD0FD2DD6B9BB3D5E48917AA31775422439745C4541`
- `out/logs/postonline031-result.log` -> `postonline031-result.log`, SHA256 `29C31D6B48220C83BB43222AA84891BCC635BBE4CE5613EF1DA77D92BC38C8F7`

## 私有整片备份

- `C:/Users/cxh/.codex/private/patrol-jlink-0.2.31-20260915/before.bin`, SHA256 `844912D3B60D40D1112BFE36C03D4B580152BFD29413D5946F6306851C2CD13A`
- `C:/Users/cxh/.codex/private/patrol-jlink-0.2.31-20260915/after.bin`, SHA256 `CF6E823401E77A7D2D4AF641C64AD9F317259AC00FF2998A0D79E4D93AC71862`

远端原始目录保留在 `wyse3040:/tmp/patrol-jlink-031-3kYeZh`。

## 自适应速度现场验证

刷写后的正向、负向、回中和重复性验证已完成，结论与原始 CAN/平台证据见 [`adaptive-validation/summary.md`](adaptive-validation/summary.md)。验证覆盖 Node 1 `0x6081:00` 的 200/100/50 rpm 自适应写入及逐次读回、三次边缘到位、三次回中、双转角一致性、零轮速、无 Bus-Off/故障锁存，以及测试开关恢复。
