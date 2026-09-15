# 0.2.31 转向速度轮廓发布与 Wyse 刷写证据

本次通过 `server47:6008` 的 FRP 转发连接 `wyse3040`，使用 J-Link 69730334、STM32F105RC、SWD 4000 kHz 更新 App。刷写前平台返回设备离线，Wyse `can0` 以 500 kbit/s 被动监听 3 秒未收到 CAN 帧；因此没有发送运动或使能命令，也不能据此声称设备物理断电或现场运动安全已经验收。

正式 App 为 `patrol-chassis-0.2.31`，109128 bytes，SHA256 `C8AB6FAEAB6B3C77FAEABC39FD2A78F443F3EE4E76B9016541D6DCCF00760A34`。J-Link 写入后读回 App 与正式包逐字节一致。Boot State A 从有效序号 20、版本 0.2.30 更新为有效序号 21、版本 0.2.31；Boot State B 保持序号 19、版本 0.2.30。Boot、配置页和 Boot State B 的读回字节均与刷写前一致。

平台在复位后仍返回设备离线，故本记录只证明 J-Link 写入、状态页更新和 Flash 读回一致，不证明 MQTT 已上线、Boot 已确认新版本，也不证明真实转向、行走、电源输出或现场验收通过。

## 归档文件

- `out/logs/wyse-031-backup.log` -> `wyse-031-backup.log`, SHA256 `0C49DCE5B8D43FE7295F59D96AA4A9EA5DBF477C55E05DAC06FE7BD9812C11CD`
- `out/logs/wyse-031-flash.log` -> `wyse-031-flash.log`, SHA256 `4253ED179EC66A7EBD591BEBB9616555FC229F23F36A7059D306036F74F1B4F5`
- `out/logs/flash031-result.json` -> `flash031-result.json`, SHA256 `7D31C327E2D40127D8A7B0AF513A60F33D02D772708E26C5D59E52653C8C2E5F`
- `out/logs/preflash031-result.log` -> `preflash031-result.log`, SHA256 `797047E54E0506C12762A5C20832B5E5A8A34EDA1933B421C2C4B9961F68845B`
- `out/logs/postflash031-result.log` -> `postflash031-result.log`, SHA256 `D2E9C8EA624FECCF95549FD0FD2DD6B9BB3D5E48917AA31775422439745C4541`

## 私有整片备份

- `C:/Users/cxh/.codex/private/patrol-jlink-0.2.31-20260915/before.bin`, SHA256 `844912D3B60D40D1112BFE36C03D4B580152BFD29413D5946F6306851C2CD13A`
- `C:/Users/cxh/.codex/private/patrol-jlink-0.2.31-20260915/after.bin`, SHA256 `CF6E823401E77A7D2D4AF641C64AD9F317259AC00FF2998A0D79E4D93AC71862`

远端原始目录保留在 `wyse3040:/tmp/patrol-jlink-031-3kYeZh`。
