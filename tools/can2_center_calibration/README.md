# CAN2 本地中位标定工具

本目录用于没有 4G/网页平台时，通过 CAN2 调用 A-BOX 内与网页完全相同的中位标定状态机。脚本只发送 MCU 的 `0x520/0x530` 维护协议，不直接访问驱动器 CAN1，不写电机参数，也不伪造 Config V5。

当前源码支持上电运行坐标建立与标定维护互锁；源码验证及现场状态见 `docs/CURRENT.md`。正式发布固件以根目录 `dist/release.manifest.json` 为准。没有 CAN2 标定通道的旧固件可能表现为 `0x530` 无响应。

本目录主机脚本版本为 `1.2.0`。脚本接受三帧乱序、重复和迟到应答；只读查询缺帧时使用新序号重试。`PREPARE` 可安全重试，`START/COMMIT` 不属于幂等操作，不会盲目重发，而是通过只读状态查询恢复会话令牌或确认提交阶段。提交结束后仍以 `COMPLETE`、`calibrated=true`、保存中位与捕获中位差不超过 5 count 作为成功依据。

## 接线

- 当前车辆接线为 CANalyst-II 物理 CH1 接 A-BOX CAN2 的 H/L/GND，物理 CH2 接 A-BOX CAN1。
- 波特率 500 kbps。
- A-BOX CAN1 保持连接转向、行走驱动器；独立转向绝对编码器保持原 UART5 接线。
- 关闭其他占用同一 CANalyst-II 的设备或软件。

包装脚本默认选择分析仪 CH1。若实际接线改为物理 CH2，请显式增加 `-Channel 2`。

如现场总线负载较高，可在不修改源码的情况下增加单次三帧应答等待时间或只读查询次数：

```powershell
.\tools\can2_center_calibration\run_center_calibration.ps1 status `
  -ResponseTimeout 1.5 -QueryRetries 4
```

`-ResponseTimeout` 允许范围为 `0.2～10.0` 秒，`-QueryRetries` 允许范围为 `1～10` 次。增加的是只读恢复机会，不会让脚本自动重发 `START`、`COMMIT` 或运动命令。

## 前置条件

- 已烧录包含 CAN2 中位标定通道的固件。
- 车辆架空或具备随时可用的急停。
- A-BOX、转向节点、行走节点和编码器在线，双反馈新鲜。
- 无 Bus-Off、驱动故障、OTA 操作或其他控制会话。
- 现场人员已确认转向轮处于机械中位。

开始标定前必须松开遥控器摇杆并关闭 T12 的 A 开关，等待至少 3 秒。若 motion `flags` 的 bit 5 为 1，表示其他控制会话仍占用底盘，脚本会直接拒绝提交。`PREPARE` 至会话结束期间，普通接管及自动回中被禁止；结束后 T12 需重新释放 A 再开启，不恢复旧目标。

## 按当前中位一键标定

可直接使用 PowerShell 包装脚本：

```powershell
.\tools\can2_center_calibration\run_center_calibration.ps1 current
```

默认使用 CANalyst-II CH1 和现有 64 位 ControlCAN.dll 路径。也可直接调用 Python：

```powershell
python .\tools\can2_center_calibration\can2_center_calibration.py `
  --channel 0 `
  --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll" `
  calibrate-current
```

直接调用 Python 时，`--channel`、`--dll`、`--response-timeout` 和 `--query-retries` 放在子命令之前；PowerShell 包装脚本不受此顺序影响。

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

如果 `COMMIT` 的三帧应答不完整，脚本会立即查询状态机：阶段已经离开 `READY` 并进入 `COMMIT_STABLE/ENCODER_WRITE/ENCODER_VERIFY/COMPLETE` 时继续完成读回校验；仍停留在 `READY` 时明确报错，并要求人工检查后重新执行，不会自动重复提交。

需要由操作者输入 `YES` 确认机械中位。自动化和已经完成双人确认时可加 `--yes`。

如果显示存在锁存故障，先排除故障原因，再显式增加：

```powershell
.\tools\can2_center_calibration\run_center_calibration.ps1 current -ResetFault
```

该参数只会执行 A-BOX 既有的软件故障复位和撤电流程，不代表物理故障已经消失。

也可以先只清软件故障、不启动标定：

```powershell
.\tools\can2_center_calibration\run_center_calibration.ps1 reset-fault
```

执行后必须重新运行 `status`，确认 motion `flags` 不再包含 bit 3。若仍锁存，继续检查真实执行故障，不能反复清锁。

## 查询

```powershell
python .\tools\can2_center_calibration\can2_center_calibration.py --channel 0 `
  --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll" `
  status
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
python .\tools\can2_center_calibration\can2_center_calibration.py --channel 0 `
  --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll" `
  interactive
```

交互命令：

```text
status
power-on
start
jog 200       # +0.2°，允许 ±200
jog -1000     # -1°，允许 ±1000
jog 5000      # +5°，允许 ±5000
watch         # 持续显示状态，仍可输入下一条命令，无需 Ctrl+C
commit
cancel
power-off
quit
```

`power-on` 和 `start` 自动执行 `PREPARE`；也可显式输入 `prepare`。交互模式等待人工输入时仍每约 200 ms 保活，所有 CAN 事务由同一个线程串行执行；`watch` 不阻塞命令输入。超过 3 秒没有匹配命令、CAN2 Bus-Off 或 OTA 开始会由固件取消标定，READY 的 10 分钟空闲期限不因查询延长。START 最长等待 7 秒，并在明确拒绝时刷新幂等 PREPARE；响应不确定时只读恢复，不盲目重发 START/COMMIT。

## 断电与验证

提交完成时，驱动器中位保存到主控 Config V5 `0x0803F800`，绝对编码器零点写入编码器本体。整车断电重启后，主控会重新加载 Config V5，但 RAM 运行坐标会重新建立。

标定成功后至少执行一次完整断电重启，再运行 `status`，确认：

- `calibrated=true`；
- `saved_center` 等于本次提交时显示的当前中位；
- `encoder_raw` 为 `0/1/2/4094/4095` 之一；
- 转向和行走节点在线，双角度一致且在允许范围；
- 车辆不会自动接管或运动。

协议定义见 `docs/protocols/巡防车底盘本地CAN控制协议.md`。
