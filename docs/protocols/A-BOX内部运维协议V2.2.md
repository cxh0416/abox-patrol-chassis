# 巡防车底盘内部运维接口 V2.2

本文只定义 `patrol_vehicle_chassis` 当前内部运维接口。**V2.2 是本文档修订号，报文 `version` 固定为 `"2.0"`**。公共 MQTT V4 外层和通用规则见 [ABox Platform 协议](https://github.com/cxh0416/abox-platform-firmware/blob/master/docs/protocols/A-BOX设备公共MQTT协议-V4.0.md)；车辆上下电、连续控制及状态上报见[底盘业务 Profile](A-BOX巡防车底盘业务协议-V4.0.md)。内部接口不承载公共底盘控制。

## 身份、Topic 与报文

`{deviceId}` 是当前 VID。内部响应和日志的 `data.product` 固定为 `patrol_vehicle_chassis`；请求可以省略该字段，携带时必须匹配。平台按精确 Topic、产品和 `requestId` 关联响应，不用日志创建设备或刷新在线状态。

| 方向 | Topic | QoS | 内容 |
| --- | --- | ---: | --- |
| 平台 → 设备 | `/zxwl/abox/{deviceId}/internal/config` | 1 | 查询、配置和维护请求 |
| 设备 → 平台 | `/zxwl/abox/{deviceId}/internal/config/resp` | 1 | 对应响应 |
| 平台 → 设备 | `/zxwl/abox/{deviceId}/internal/ota` | 1 | OTA 请求 |
| 设备 → 平台 | `/zxwl/abox/{deviceId}/internal/ota/resp` | 1 | OTA 响应 |
| 设备 → 平台 | `/zxwl/abox/{deviceId}/internal/log` | 主动日志 0；查询分片 1 | 日志与告警 |

以上 Topic 均不设置 Retain。设备只订阅本 VID 的精确请求 Topic。内部请求完整 UTF-8 JSON 最多 1023 字节，设备单条响应或日志 JSON 小于 1080 字节；超限内容须分片或返回摘要，不得截断 JSON。

配置和查询使用同一信封；`requestId` 重试时保持不变：

```json
{"version":"2.0","timestamp":1788134400000,"data":{"requestId":"uuid","command":"internal_config","params":{"action":"get_info"}}}
```

响应信封如下；`code` 表示本次接口的接受或完成边界，不能替代现场反馈：

```json
{"version":"2.0","timestamp":1788134400123,"data":{"product":"patrol_vehicle_chassis","requestId":"uuid","code":200,"msg":"OK","result":{}}}
```

设备可能在前一次响应、日志快照或重启请求尚未完成时返回 `DEVICE_BUSY`。接收方应忽略 `result` 中不认识的兼容扩展字段。

## `internal_config` 动作

| 类别 | `params.action` | 必要参数 | 响应与接口语义 |
| --- | --- | --- | --- |
| 只读 | `get_info` | 无 | App、Boot、OTA、MQTT、电源、CAN 摘要及 `resetReason`、`uptimeMs` |
| 只读 | `get_can_diag` | 无 | CAN、节点、反馈、编码器和 SDO 诊断；不发送运动或电源命令 |
| 只读 | `get_calibration` | 无 | 逐车标定参数、`calibrated`、`remoteEnabled` 和配置兼容状态 |
| 只读 | `get_bench_steering` | 无 | 台架许可、`driveLimitRpm`、门禁、阶段和故障状态 |
| 只读 | `get_center_calibration` | `sessionId` | 查询并续租标定会话；返回阶段、坐标、`failure` 和 `lastReject` |
| 配置 | `set_mqtt` | `vid`、`host`、`port`、`username`、`password`、`tlsEnabled`、`tlsProfileId` | 两阶段响应，见下文 |
| 标定 | `set_calibration` | `steeringCenterCount`、`absoluteCenterCount` | 保存逐车参数后保持远程运动禁用；可选比例、方向和容差只接受产品固定值 |
| 标定 | `clear_calibration` | 无 | 清除标定并关闭远程运动使能 |
| 标定 | `start_center_calibration` | 无 | 建立独占标定会话，不直接运动 |
| 标定 | `jog_center_calibration` | `sessionId`、`deltaMdeg` | 一次受限转向点动；`deltaMdeg` 为 ±200、±1000 或 ±5000 |
| 标定 | `commit_center_calibration` | `sessionId` | `202 CENTER_CALIBRATION_COMMIT_STARTED` 仅表示异步提交开始，须继续查询 |
| 标定 | `cancel_center_calibration` | `sessionId` | 停止并结束标定会话 |
| 台架 | `arm_bench_steering`、`arm_bench_drive` | `limitMdeg` | 分别授予仅转向、行走加转向的短时 RAM 许可；限幅为 1000、3000、5000、20000 或 30000 |
| 台架 | `stop_bench_steering` | 无 | 撤销台架许可并请求停止；需再核验状态 |
| 故障 | `reset_motion_fault` | 无 | 仅在动力输出关闭且 OTA 空闲等门禁满足时返回 `200 FAULT_RESET_REARM_REQUIRED`；保留故障记录，不恢复旧会话 |
| 日志 | `get_log` | 无 | 日志快照发往 `/internal/log`，按 `requestId`、`sequence`、`total` 分片 |
| 设备 | `reboot` | 无 | `200 REBOOTING` 是受理响应，响应发送完成后重启 |

`set_calibration` 的可选固定字段为 `steeringCountsPerDegreeMilli`、`absoluteCountsPerDegreeMilli`、`steeringDirection`、`absoluteDirection`、`steeringToleranceMdeg`。标定和台架动作均服从设备门禁；被拒绝的中位标定动作以 `409` 返回当前诊断 `result`。`get_center_calibration` 返回的 `phase=8` 表示失败，`failure` 冻结本次会话首个失败，`lastReject` 记录最近一次拒绝。`commit`、台架许可或单次点动的成功响应均不证明真实转动或标定完成。

`get_info` 提供 `fwVersion`、Boot 描述符、`otaState`/`otaPhase`/`otaReady`、已提交的 MQTT 配置、电源命令状态及 CAN 摘要。`get_can_diag` 提供原始收帧、当前及本次启动历史 Bus-Off、节点状态字、反馈新鲜度、编码器和 SDO 计数及驱动对象最近读取值。旧值和初始零值须结合新鲜度解释，`powerOutputOn` 只是 MCU 输出命令状态，不是继电器或接触器反馈。

`get_can_diag.result` 的主要字段为 `canRx`、`can1RawRx`、`can2RawRx`、`canRxOverflow`、`canBusOff`、`canBusOffActive`，节点 `steerHb`/`driveHb`、`steerSw`/`driveSw`、`steerFresh`/`driveFresh`，实测 `leftRpm`/`rightRpm`、`steerRaw`、`encoderRaw`/`encoderOnline`，以及 `sdoOk`/`sdoTimeouts`/`sdoAborts`、`driveObjects`、`sdoReadMask`/`sdoAbortMask`。`canBusOff` 是本次启动的发生记录，`canBusOffActive` 是查询时状态；当前运动门禁另以最新状态上报为准。

### `set_mqtt` 响应

全部参数构成一次不可拆分的候选配置。`port` 范围为 1～65535，`tlsProfileId` 范围为 0～255。设备先在旧连接返回 `202 MQTT_CANDIDATE_TESTING`；候选连接和业务证明成功、配置保存及回读完成后，使用**同一 `requestId`**返回 `200 MQTT_CONFIG_SAVED`；失败并恢复原连接后返回 `500 MQTT_CANDIDATE_RESTORED`。试连中的重试不另起事务。只有最终 `200` 表示新配置成为 active；`get_info` 只报告已提交配置。TLS 失败不会自行改用明文。

### 旧平台兼容

固件仍识别内部 `power_on`、`power_off`，仅用于已部署旧平台的兼容。当前平台必须使用公共 `vehicle_chassis 1.0` Profile 的上下电命令。实验性 MQTT runtime 动作不属于当前平台接口。

## OTA

OTA 使用 `/internal/ota`，请求 `data.command="ota_upgrade"`，`params` 必须携带 HTTPS `url`、目标固件 `version`、正整数 `size` 和八位十六进制 `crc32`。版本须符合 `patrol-chassis-` 格式；文件不得超过产品 App 区 `0x36000` 字节；URL 须通过产品来源、目录、版本和制品名校验。响应仍按 `product` 和 `requestId` 关联。

`200 INSTALL_PENDING` 仅表示候选镜像校验完成并提交安装。平台须继续查询 `get_info`：只有实际 `fwVersion` 等于目标版本且 `otaState=CONFIRMED` 才能判定完成；`TRIAL` 尚未确认，`ROLLBACK` 表示本次升级失败。OTA 忙或 Boot 不允许时不得开始新任务，失败不得破坏稳定镜像。

## 边界

内部接口不承载公共 `chassis_control` 或 `chassis_status`。查询、命令 ACK、构建成功和候选安装都不能证明真实运动、供电、转角或 OTA 现场验收。标定、台架许可、复位、重启和 OTA 不绕过设备安全门禁；当前操作步骤见 [逐车标定](../operations/巡防车底盘逐车标定与参数记录.md)和[故障复位](../operations/故障记录与复位.md)。
