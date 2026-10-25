# CAN2 本地中位标定工具

本目录用于没有 4G/网页平台时，通过 CAN2 调用 A-BOX 内与网页完全相同的中位标定状态机。脚本只发送 MCU 的 `0x520/0x530` 维护协议，不直接访问驱动器 CAN1，不写电机参数，也不伪造 Config V5。

配套固件候选为 `patrol-chassis-0.2.88`。源码候选位于 `out/candidates/bench-drive/`；完整烧录包位于本次 `docs/operations/evidence/2026-10-24-can2-multiframe-reply-fix-*` 证据目录。没有该 CAN2 标定通道的旧固件会表现为 `0x530` 无响应；`0.2.88` 修复了发送邮箱暂时繁忙时丢失 `0x532/0x533` 或 `0x522/0x523` 后续帧的问题。

## 接线

- 当前车辆接线为 CANalyst-II 物理 CH1 接 A-BOX CAN2 的 H/L/GND，物理 CH2 接 A-BOX CAN1。
- 波特率 500 kbps。
- A-BOX CAN1 保持连接转向、行走电机和绝对编码器。
- 关闭其他占用同一 CANalyst-II 的设备或软件。

包装脚本默认选择分析仪 CH1。若实际接线改为物理 CH2，请显式增加 `-Channel 2`。

## 前置条件

- 已烧录包含 CAN2 中位标定通道的固件。
- 车辆架空或具备随时可用的急停。
- A-BOX、转向节点、行走节点和编码器在线，双反馈新鲜。
- 无 Bus-Off、驱动故障、OTA 操作或其他控制会话。
- 现场人员已确认转向轮处于机械中位。

开始标定前必须松开遥控器摇杆并关闭 T12 的 A 开关。若 motion `flags` 的 bit 5 为 1，表示其他控制会话仍占用底盘，脚本会直接拒绝提交。

## 按当前中位一键标定

可直接使用 PowerShell 包装脚本：

```powershell
.\can2_center_calibration\run_center_calibration.ps1 current
```

默认使用 CANalyst-II CH1 和现有 64 位 ControlCAN.dll 路径。也可直接调用 Python：

```powershell
python .\can2_center_calibration\can2_center_calibration.py `
  calibrate-current `
  --channel 0 `
  --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll"
```

脚本会依次执行：

1. 查询当前状态；
2. 检查运动故障和其他控制会话，并要求操作者确认机械中位；
3. `PREPARE` 抑制自动回中；
4. 开启车辆电源输出命令；
5. `START` 建立独占标定会话；
6. 等待连续三组稳定反馈和 `READY`；
7. `COMMIT` 提交当前驱动器中位和编码器中位；
8. 等待编码器三次零点确认及 Config V5 保存、读回；
9. 只有状态达到 `COMPLETE` 且 `calibrated=true` 才算成功；
10. 最后发送 `POWER_OFF`。

需要由操作者输入 `YES` 确认机械中位。自动化和已经完成双人确认时可加 `--yes`。

如果显示存在锁存故障，先排除故障原因，再显式增加：

```powershell
.\can2_center_calibration\run_center_calibration.ps1 current -ResetFault
```

该参数只会执行 A-BOX 既有的软件故障复位和撤电流程，不代表物理故障已经消失。

也可以先只清软件故障、不启动标定：

```powershell
.\can2_center_calibration\run_center_calibration.ps1 reset-fault
```

执行后必须重新运行 `status`，确认 motion `flags` 不再包含 bit 3。若仍锁存，继续检查真实执行故障，不能反复清锁。

## 查询

```powershell
python .\can2_center_calibration\can2_center_calibration.py status --channel 0 `
  --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll"
```

重点查看：

- `driver_raw`：当前 Node 1 `0x6064` 原始计数；
- `saved_center`：Config V5 中已保存的驱动器中位；
- `encoder_raw`：绝对编码器当前计数；
- `phase_name`：`READY/COMMIT_STABLE/ENCODER_WRITE/ENCODER_VERIFY/COMPLETE/FAILED`；
- `reason_name`：失败或最近一次拒绝原因；
- `calibrated`：持久配置是否有效。

断电状态下 `driver_raw` 可能显示缓存占位值 `0`，因为固件在动力关闭时不继续轮询位置；此时应结合 `power_on=false` 和独立编码器值判断，不能把该占位值当作实时 `0x6064`。

## 点动和分步标定

如果车轮尚未在机械中位，可运行交互模式：

```powershell
python .\can2_center_calibration\can2_center_calibration.py interactive --channel 0 `
  --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll"
```

交互命令：

```text
status
power-on
start
jog 200       # +0.2°，允许 ±200
jog -1000     # -1°，允许 ±1000
jog 5000      # +5°，允许 ±5000
watch
commit
cancel
power-off
quit
```

活动标定会话需要持续查询，脚本每 200 ms 查询一次；超过 3 秒没有匹配命令、CAN2 Bus-Off 或 OTA 开始会由固件取消标定。

## 断电与验证

提交完成时，驱动器中位保存到主控 Config V5 `0x0803F800`，绝对编码器零点写入编码器本体。整车断电重启后，主控会重新加载 Config V5，但 RAM 运行坐标会重新建立。

标定成功后至少执行一次完整断电重启，再运行 `status`，确认：

- `calibrated=true`；
- `saved_center` 等于本次提交时显示的当前中位；
- `encoder_raw` 为 `0/1/2/4094/4095` 之一；
- 转向和行走节点在线，双角度一致且在允许范围；
- 车辆不会自动接管或运动。

协议定义见 `docs/protocols/巡防车底盘本地CAN控制协议.md`。
