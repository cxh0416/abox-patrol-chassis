# 巡防车底盘 A-BOX 内部运维协议

协议修订：1.0

契约状态：内部运维接口定义；具体实现、发布和现场验收状态见内部运维与验收文档

产品标识：`patrol_vehicle_chassis`

## 1. 协议定位

本文定义 OTA 平台与巡防车底盘 A-BOX 固件之间的设备信息、日志、远程电源输出、受控重启、MQTT 配置和 Boot V2 OTA 接口。它们使用 `/internal/*` Topic，不属于公共业务接口；底盘控制和状态见 [A-BOX 车辆底盘业务协议 V3](../protocols/A-BOX车辆底盘业务协议-V3.0.md)。厂商 CANopen 对象字典和执行约束不在本文定义。

公共业务与内部运维使用两套明确身份：

- 公共 V3 请求、响应、状态和心跳使用 `data.deviceType=vehicle_chassis`，不得携带 `data.product`。
- 内部 V2 响应和日志使用 `data.product=patrol_vehicle_chassis`，不得用 `deviceType` 代替。

## 2. Topic 与交付等级

`{deviceId}` 与设备当前 VID 为同一值。

| 方向 | 用途 | Topic | QoS | Retain |
|---|---|---|---:|---:|
| 平台 -> A-BOX | 内部配置和查询 | `/zxwl/abox/{deviceId}/internal/config` | 1 | 否 |
| A-BOX -> 平台 | 内部配置响应 | `/zxwl/abox/{deviceId}/internal/config/resp` | 1 | 否 |
| 平台 -> A-BOX | Boot V2 OTA | `/zxwl/abox/{deviceId}/internal/ota` | 1 | 否 |
| A-BOX -> 平台 | Boot V2 OTA 响应 | `/zxwl/abox/{deviceId}/internal/ota/resp` | 1 | 否 |
| A-BOX -> 平台 | 设备日志 | `/zxwl/abox/{deviceId}/internal/log` | 0；查询日志分片为 1 | 否 |

A-BOX 只订阅自身 VID 的精确配置和 OTA Topic。平台重试必须沿用原 `requestId`；内部响应优先于日志发送。

## 3. 请求与响应

内部请求使用 V2 外层。为兼容现有平台，请求中的 `data.product` 可省略；若携带，必须为 `patrol_vehicle_chassis`。

```json
{
  "version": "2.0",
  "timestamp": 1788134400000,
  "data": {
    "requestId": "uuid",
    "command": "internal_config",
    "params": {"action": "get_info"}
  }
}
```

所有内部配置响应和 OTA 响应必须携带固定产品标识：

```json
{
  "version": "2.0",
  "timestamp": 1788134400123,
  "data": {
    "product": "patrol_vehicle_chassis",
    "requestId": "uuid",
    "code": 200,
    "msg": "OK",
    "result": {}
  }
}
```

平台必须同时按精确 Topic、`data.product` 和 `requestId` 关联响应。缺少 `product`、产品不匹配或版本不是 `2.0` 的共享 A-BOX 内部消息必须丢弃，不能创建设备、刷新在线状态、完成操作或改变 OTA 任务。

产品与公共能力的固定映射如下，不得混用字段：

| 作用域 | 字段 | 固定值 |
|---|---|---|
| 公共业务 V3 | `data.deviceType` | `vehicle_chassis` |
| 内部运维 V2 | `data.product` | `patrol_vehicle_chassis` |
| 固件/OTA 制品 | product | `patrol_vehicle_chassis` |

## 4. 内部操作

`internal_config` 当前支持：

| `params.action` | 说明 |
|---|---|
| `get_info` | 查询 App、Boot、OTA、MQTT 和 CAN 基本信息 |
| `get_can_diag` | 查询 CAN 节点、反馈时效及 SDO 读取诊断，不改变总线或运动状态 |
| `get_calibration` | 查询当前轮胎转角标定参数和标定状态 |
| `set_calibration` | 在远程控制禁用、车辆静止、反馈完整且控制器未使能时保存逐车转角标定；保存后继续保持远程控制禁用 |
| `clear_calibration` | 清除标定状态并关闭远程控制使能 |
| `start_center_calibration` | 创建受限转向中位标定会话；不直接执行运动 |
| `get_center_calibration` | 查询标定阶段、两路原始计数、累计调整和失败原因 |
| `jog_center_calibration` | 在标定会话内执行一次固定小步点动 |
| `commit_center_calibration` | 确认机械中位，执行编码器 2048 写入及逐车中位保存 |
| `cancel_center_calibration` | 停止点动、撤销许可并结束标定会话 |
| `get_log` | 获取内存日志快照 |
| `reboot` | 响应成功发布后受控重启 |
| `power_on` | 将车辆电源控制输出置为高电平 |
| `power_off` | 将车辆电源控制输出置为低电平 |
| `set_mqtt` | 保存新的 VID 和 MQTT 参数后重启 |

`power_on` / `power_off` 成功响应的 `result.powerOutputOn` 表示 MCU 输出锁存的命令状态。该状态不是继电器触点或整车供电反馈，不得用来声称车辆已真实开机或关机。

`get_info` 的 `result.diagnostics` 是只读联调快照：`canRx` 为 CAN1 已接收标准数据帧计数，`steerHb/driveHb` 为 Node 1/2 心跳在线判断，`steerRaw`、`leftRpm/rightRpm` 和 `encoderRaw` 为最近原始反馈，`encoderOnline` 为编码器反馈时效判断，`sdoTimeouts/encoderTimeouts` 为累计超时计数。上述字段只用于诊断，不代表标定、运动或验收成功。

`set_calibration` 的参数为 `steeringCenterCount`、`absoluteCenterCount`、`steeringCountsPerDegreeMilli`、`absoluteCountsPerDegreeMilli`、`steeringDirection`、`absoluteDirection` 和 `steeringToleranceMdeg`。每度计数均以轮胎实际转角为角度基准，并放大 1000 倍保存；方向只能为 `1` 或 `-1`，允许偏差范围为 `1..2000 mdeg`。设备仅在远程控制已禁用、车辆电源输出已开启、CAN 无 Bus-Off、两个节点和三路反馈均有效、左右实测转速不超过 20 rpm、控制器无故障且转向未处于 Operation Enabled 时接受写入；当前两路原始值换算后还必须位于所提交中位的允许偏差内。成功保存不启用远程运动。

`get_calibration` 返回上述参数，并返回 `calibrated` 和 `remoteEnabled`。配置页 V4 新增 `steeringCountsPerDegreeMilli`；从 V3 加载时保留设备身份、MQTT、诊断和 OTA 参数，但主动清除 `calibrated` 与 `remoteEnabled`，要求按轮胎实际转角重新标定。

周期状态的 `data.transportDiagnostics` 用于接收链路诊断：`mqttRxLines`、`mqttRxDecoded`、`mqttRxDispatched` 分别表示 EC800 上报的 `+QMTRECV` 行数、完成解码数和交给业务回调的消息数；`mqttRxRejected`、`mqttRxOverwrites`、`mqttRxTimeouts` 表示拒绝帧、待处理覆盖和多行超时；`ecDmaOverflows`、`ecDmaRestarts`、`ecDmaMaxBacklog`、`ecLineOverflows` 表示 USART1 DMA 软件环溢出、重启、历史最大积压字节数和 AT 行缓冲溢出。计数从本次上电开始累计，只用于区分 Broker 到模组、DMA、AT 行解析和业务派发阶段，不证明某条命令已经完成。为保持 `get_info` 单包边界，完整传输计数不重复放入控制响应。

OTA 请求使用 `command=ota_upgrade`，`params` 必须包含不可变 HTTPS `url`、目标 `version`、正整数 `size` 和八位十六进制 `crc32`。`code=200` 只表示候选镜像已校验并进入待安装状态，不代表安装、试运行或确认成功；平台仍须通过 `get_info` 和新 App 版本确认 `otaState=CONFIRMED`。

## 5. 日志

查询日志分片和主动告警都发布到 `/internal/log`，每条消息必须携带 `data.product=patrol_vehicle_chassis`。查询分片还携带 `requestId`、`sequence`、`total` 和 `lines`；主动告警携带 `level` 和 `lines`。

日志是诊断流量，不是设备在线或产品归属证据。平台只能在适配器完成 `version` 与 `product` 校验后保存日志，且不得由日志创建或刷新设备记录。公共业务平台不得订阅或解释该 Topic。


## CAN 独立只读诊断

`get_can_diag` 的请求与 `get_info` 使用相同信封和 Topic，只将 `params.action` 改为 `get_can_diag`。成功响应 `code=200`，`result` 含：

- `canRx`、`can1RawRx/can2RawRx`、`canIgnored`、`canRxOverflow`、`canBusOff`：业务 CAN 接收计数、两个 CAN 外设进入中断的原始帧计数、未匹配事务帧计数、接收溢出和本次启动以来锁存的 Bus-Off。若 `canRx=0` 而 `can2RawRx` 增长，说明现场总线进入了 CAN2；若两个原始计数均为 0，应检查收发器待机、电源和 CAN 接线。`canBusOffActive` 单独表示 CAN1 当前是否处于 Bus-Off；自动恢复不清除安全锁存。
- `steerHb/driveHb`、`steerNmt/driveNmt`：节点心跳在线状态及最近 NMT 状态字节。
- `steerFresh/driveFresh`：转向原始位置、左右实测转速是否各自处于反馈有效期；不依赖独立编码器，不等同于整车 `feedbackFresh` 或 `safetyReady`。
- `steerRaw`、`leftRpm/rightRpm`、`steerSw/driveSw`：最近转向原始位置、左右转速和两个状态字。数据过期时保留上次值，必须结合时效字段使用。
- `sdoOk`、`sdoTimeouts`、`sdoAborts`、`lastAbort`、`sdoTxErrors`：成功应答、等待超时、明确 Abort、最近 Abort 码和发送错误的累计计数。
- `driveObjects`：Node 2 的 `2000～2006` 最近读取值，按索引升序排列；未成功读取的初始零值不代表真实反馈。
- `sdoReadMask/sdoAbortMask`：从本次启动以来各对象曾成功/曾返回 Abort 的位图，按以下顺序从 bit 0 到 bit 11：Node1 `6041,6064`，Node2 `6041,6069,6869,2000,2001,2002,2003,2004,2005,2006`。位图不代替实时反馈有效期。

此查询不发送 NMT、SDO 下载、PDO 目标、驱动使能或电源命令。

## 车型标定与台架转向接口

`set_calibration` 必须提供两路中位；每度计数、方向及容差可省略，省略时采用产品公共参数，提供时必须与公共参数一致。`get_calibration.result.profileCompatible` 表示已保存参数是否匹配产品配置；不匹配时禁止运动。

- `arm_bench_steering`：参数 `limitMdeg` 允许 1000、3000、5000、20000、30000。条件不满足返回 409 `BENCH_NOT_READY`。成功返回 RAM 测试许可和 `sessionId`，15 秒内须以该会话发起公共 `chassis_control.acquire`。
- `arm_bench_drive`：参数 `limitMdeg` 同上，申请行走与转向测试许可。成功返回 `drivePermitted=true`；行走目标绝对值不得超过返回的 `driveLimitRpm`。条件不满足返回 409。
- `get_bench_steering`：返回 available、permitted、phase、step、fault、limitMdeg、sessionId、contactorFeedbackAvailable、safetyProfile、profileReady 和 rangeValid。`safetyProfile=single_turn_4096_center2048_limit30_v1` 表示当前接口所要求的安全配置。phase 为 0 空闲、1 准备、2 运行、3 停止中、4 故障；step 仅供维护诊断。
- `stop_bench_steering`：立即撤销许可并请求撤使能；200 表示停止请求已接受，须继续核验状态。

转向许可只接受 driveRpm=0；行走许可允许限速范围内的行走目标，转角不得超出本次幅度。查询额外返回 driveAvailable、drivePermitted、drivePhase、driveFault、driveLimitRpm；drivePhase 与 phase 枚举相同。停止、租约到期、故障或重启撤销许可；重新许可会生成新的 sessionId。许可不写入持久配置。contactorFeedbackAvailable=false 时没有触点状态反馈，不得推断真实供电状态。

## 转向中位标定接口

- `start_center_calibration` 无参数。设备只在 OTA 空闲、普通远控和其他台架许可关闭、节点及反馈在线、CAN 正常、驱动无故障、行走未使能且左右轮连续满足静止要求时创建会话。已有有效的两路标定且角度一致，才允许后续电机点动。
- `jog_center_calibration` 必须携带 `sessionId` 和 `deltaMdeg`；`deltaMdeg` 只允许 `-1000`、`-200`、`200`、`1000`。会话以两路原始值建立临时相对坐标，连续校验相对变化；累计目标必须处于 `-60000..60000 mdeg`，并始终服从驱动器当前软限位。
- `commit_center_calibration` 只携带 `sessionId`，不接受调用方提供编码器中位。设备固定执行编码器 2048 中位写入、读回校验及转向驱动器当前原始计数保存；响应已接受不表示整个异步提交已经完成。
- `get_center_calibration` 必须携带 `sessionId`，返回 `phase`、`sessionId`、`fault`、`driverRaw`、`encoderRaw`、`targetMdeg`、`jogOffsetMdeg`、`rangeValid` 和 `calibrated`。标定可调整阶段以该查询作为短租约心跳；连续约 4 秒未收到有效查询时设备停止标定并关闭车辆电源输出。平台必须轮询至完成或失败，并结合后续掉电重启与反馈验证判断结果。
- `cancel_center_calibration` 必须携带 `sessionId`。浏览器离开、网络异常、会话过期或人工停止时，平台应提交取消；设备停止执行、撤销许可并关闭车辆电源输出。

标定任一步失败或掉电后，设备不得凭 MQTT 响应建立正式运行坐标。提交已经清除旧标定后发生失败时保持未标定；编码器写入结果不确定时只能通过后续读回判断，不回滚编码器。
