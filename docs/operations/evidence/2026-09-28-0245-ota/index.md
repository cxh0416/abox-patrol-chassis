# 0.2.45 正式打包与单车 OTA 记录

2026-09-28，目标 VID `PATROL-05D3FF343733574E43043550`。本次只执行固件发布和 OTA；未发送上电、标定或运动命令。平台远控总开关保持关闭。

## 固件身份与主机验证

- 源码提交 `54d8f6bb4763c6360d9559d054f4aa3275fe06fb`，正式包提交 `cb66fed48308e9d746c837c6dfb8836534d569b9`。
- `tools/run_tests.ps1` 通过，包括产品 Python/主机 C 测试及 Platform CTest 21/21；ARM Release 候选与正式重建、栈和 Flash 分区检查通过。
- `dist/release.manifest.json`：`patrol-chassis-0.2.45`，`PATROL_CHASSIS_ACTUATION_ENABLED=ON`，转向及行走执行器 ON，启动运动许可 false。
- 正式 App 与候选逐字节相同，均为 125368 字节，SHA256 `7A4E5FE56F15E588B0672DE06A7731447D376FB1008E9F019BC175CD768AF666`。冻结 Boot SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63`。
- 服务器 immutable revision 与本地正式 App 的 SHA256 一致；服务器发布操作经服务账号执行并写入审计。

## 单车 OTA

- 下发前平台记录在线版本为 `patrol-chassis-0.2.44`；Boot V2 能力来自该版本 `get_info`。无活动控制、标定、维护或 OTA 任务。最近状态为 `stopped`、目标行走 0、左右实测 0 rpm、`otaBusy=false`。当时 `driveFault=true`，运动安全门关闭。
- OTA 任务 `0edbe7ae-c9a5-448e-a07d-2edea39218be`，目标仅此一车，服务器记录 `INSTALL_PENDING`，随后目标状态 `completed`，确认时间 2026-09-28 09:43:10 UTC。
- 升级后独立 `get_info` 操作 `60861ccb-624e-4488-8460-71bb630bbd74` 成功返回：`fwVersion=patrol-chassis-0.2.45`、`otaTargetVersion=patrol-chassis-0.2.45`、`otaState=CONFIRMED`、`otaPhase=IDLE`、`otaBusy=false`、`otaReady=true`、`lastOtaError=0`、`trialFailCount=0`。Boot 为 `abox-boot-2.3.0`，描述符有效，ABI 1，features 15；`powerOutputOn=false`。
- 升级后的新状态报告显示设备在线、`calibrated=true`、`motionState=stopped`、左右实测 0 rpm、`driveFault=false`、`faultLatched=false`、`powerOutputOn=false`。

OTA 任务、`get_info` 和状态上报证明本次软件升级与设备重新上线；它们不证明真实运动、转角、停车或逐车 Config V5 的物理回读。上述运动回归和标定持久化复验尚未执行。
