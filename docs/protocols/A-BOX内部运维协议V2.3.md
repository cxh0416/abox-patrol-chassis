# 巡防车底盘内部运维协议 V2.3

## 1. 范围

本规范定义产品 `patrol_vehicle_chassis` 的内部运维 MQTT 请求、响应和标定接口。公共 MQTT V4 信封、上下电、`chassis_control`、`chassis_status` 仍由[底盘业务协议 V4.0](A-BOX巡防车底盘业务协议-V4.0.md)定义。本规范不改变公共协议版本。

本车没有独立的继电器或接触器状态反馈输入。`powerOutputOn` 仅表示 MCU 电源或使能 GPIO 命令状态。驱动器 CAN 状态、实测转速、转向位置和独立编码器是运动反馈来源；MQTT 响应不单独证明真实运动或停车。

## 2. Topic 与信封

V2.3 是本文档修订号；内部报文顶层 `version` 仍为字符串 `"2.0"`。产品标识固定为 `patrol_vehicle_chassis`，`{deviceId}` 是当前 VID。请求 JSON 最多 1023 字节，单条响应或日志 JSON 小于 1080 字节。Topic 不设置 Retain；设备只订阅本 VID 的精确请求 Topic。

| Topic | 方向 | QoS | 用途 |
| --- | --- | ---: | --- |
| `/zxwl/abox/{deviceId}/internal/config` | 平台 → 设备 | 1 | `internal_config` |
| `/zxwl/abox/{deviceId}/internal/config/resp` | 设备 → 平台 | 1 | 配置、标定和诊断响应 |
| `/zxwl/abox/{deviceId}/internal/ota` | 平台 → 设备 | 1 | `ota_upgrade` |
| `/zxwl/abox/{deviceId}/internal/ota/resp` | 设备 → 平台 | 1 | OTA 响应 |
| `/zxwl/abox/{deviceId}/internal/log` | 设备 → 平台 | 主动 0、查询分片 1 | 日志 |

请求示例：

```json
{"version":"2.0","timestamp":0,"data":{"requestId":"cal-1","command":"internal_config","params":{"action":"start_center_calibration"}}}
```

响应示例：

```json
{"version":"2.0","timestamp":0,"data":{"product":"patrol_vehicle_chassis","requestId":"cal-1","code":200,"msg":"OK","result":{"state":"BUSY","sessionId":"c-1-1","calibrated":false,"reason":"NONE","jogOffsetMdeg":0}}}
```

| 信封字段 | 类型 | 必填 | 约束与含义 |
| --- | --- | ---: | --- |
| `version` | String | 是 | 固定 `"2.0"`，与文档修订号不同。 |
| `timestamp` | Integer | 请求建议、响应必填 | Unix 毫秒时间；设备请求解析不依赖该字段。 |
| `data.requestId` | String | 是 | 1～63 个可打印 ASCII 字符；同一逻辑操作重试保持不变。 |
| `data.product` | String | 请求可选、响应必填 | 携带时必须为 `patrol_vehicle_chassis`。 |
| `data.command` | String | 请求必填 | 配置 Topic 为 `internal_config`，OTA Topic 为 `ota_upgrade`。 |
| `data.params` | Object | 请求必填 | 各动作字段见下文；不得用日志或状态上报推断命令结果。 |
| `data.code` | Integer | 响应必填 | 本次请求的接受或完成边界。 |
| `data.msg` | String | 响应必填 | 响应码对应的机器可读消息。 |
| `data.result` | Object | 响应必填 | 动作结果；失败时可能是空对象。 |

| `code` / `msg` | 接口语义 |
| --- | --- |
| `200 OK` | 查询成功或同步请求已接受；不证明物理动作完成。 |
| `202 CENTER_CALIBRATION_COMMIT_STARTED`、`202 MQTT_CANDIDATE_TESTING` | 异步过程已开始，须继续查询最终结果。 |
| `400 INVALID_REQUEST` | 信封或字段格式不符。 |
| `404 UNKNOWN_ACTION` | 生产动作不存在。 |
| `409 CENTER_CALIBRATION_NOT_READY`、`409 DEVICE_BUSY` | 当前状态拒绝操作或设备正在处理其他操作。标定拒绝优先读取 `result.reason`。 |
| `413 PAYLOAD_TOO_LARGE` | 请求超过 1023 字节。 |
| `500`、`503` | 执行、编码或设备能力错误，以 `msg`、`result` 为准。 |

接收方按精确 Topic、`product`、`requestId` 关联响应；未知的兼容扩展字段可忽略。响应、日志与 MQTT ACK 不替代实测反馈。

## 3. 内部动作

`data.command="internal_config"` 时，`data.params.action` 取下列值。未列出的生产动作返回 `404 UNKNOWN_ACTION`。

| action | 额外请求字段 | 响应或接口语义 |
| --- | --- | --- |
| `get_info` | 无 | 设备、Boot、MQTT、OTA 状态及 `powerOutputOn` 命令状态 |
| `get_log` | 无 | 日志快照分片发往 `/internal/log`，最终响应给出 `lineCount`、`dropped` |
| `get_can_diag` | 无 | CAN、驱动器和编码器诊断快照 |
| `get_calibration` | 无 | Config V5 标定值与 `profileCompatible`、`calibrated` |
| `start_center_calibration` | 无 | 申请独占标定会话，返回高层标定状态 |
| `get_center_calibration` | `sessionId` | 查询本次标定状态；查询不延长 READY 空闲期限 |
| `jog_center_calibration` | `sessionId`、`deltaMdeg` | 请求一次受限转向点动；步长只可为 `±200`、`±1000`、`±5000` 毫度 |
| `commit_center_calibration` | `sessionId` | 确认机械中位后开始写入和读回，成功请求返回 `202 CENTER_CALIBRATION_COMMIT_STARTED` |
| `cancel_center_calibration` | `sessionId` | 请求受控停止并结束标定；执行器停稳前状态可以继续为 BUSY |
| `get_center_calibration_diagnostics` | 无 | 按需读取内部阶段和故障证据，不用于平台安全裁决 |
| `reset_motion_fault` | 无 | 仅下电且 OTA 空闲时复位运动故障；旧控制会话不恢复 |
| `set_mqtt` | `vid`、`host`、`port`、`username`、`password`、`tlsEnabled`、`tlsProfileId` | MQTT 候选试连和提交，可能先返回 `202 MQTT_CANDIDATE_TESTING` |
| `mqtt_runtime_get` | 无 | 查询当前 MQTT runtime 与候选状态 |
| `reboot` | 无 | 接受重启请求；须另行观察设备重新上线 |

标定写入只能通过 `start → READY → jog → READY → commit → DONE`。生产接口没有单独的行走或转向许可动作。实验性 RAM MQTT 候选和取消动作不属于本合同；正式写入使用 `set_mqtt`。

## 4. 标定状态与拒绝原因

标定请求的 `result` 为以下高层对象；同一会话的 ID 必须原样传回。`jogOffsetMdeg` 表示设备已接受的累计点动目标，不表示实测角度。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `state` | String | `IDLE`、`READY`、`BUSY`、`DONE`、`ERROR` |
| `sessionId` | String | 当前设备标定会话 ID；空串表示无会话 |
| `calibrated` | Boolean | 兼容标定是否可用 |
| `reason` | String | 下表稳定业务原因 |
| `jogOffsetMdeg` | Integer | 已接受的累计点动目标，毫度 |

`READY` 自最后一次进入该状态起有 10 分钟纯空闲期限；读取状态不续期。到期返回 `state=IDLE, reason=EXPIRED`，结束会话并关闭电源输出命令，不生成设备故障或故障记录。`DONE` 表示本次提交已完成编码器验证、Config V5 保存及回读；它只供本次事务短暂观察，随后回到 `IDLE, calibrated=true`。设备重启只恢复持久 `calibrated`，不恢复 `DONE`。平台错过 `DONE` 时可调用 `get_calibration` 核对持久标定。

请求拒绝仍可返回当前状态和 `reason`。`ERROR` 表示设备执行故障、持续安全异常或受控停止失败，详情需按需查询诊断。平台不得根据内部阶段、反馈年龄或门禁位放行运动。

| `reason` | 含义 |
| --- | --- |
| `NONE` | 无业务拒绝原因。 |
| `INVALID_STATE` | 当前状态或操作顺序不允许本动作。 |
| `SESSION_MISMATCH` | 会话 ID 不匹配。 |
| `EXPIRED` | READY 纯空闲期限已到，当前会话结束。 |
| `SAFETY_BLOCKED` | MCU 当前安全条件或执行器状态阻止操作。 |
| `OUT_OF_RANGE` | 点动步长、累计边界或编码器原始范围无效。 |
| `EXECUTION_FAILED` | 点动、停止、编码器执行或验证失败。 |
| `PERSIST_FAILED` | Config V5 清除、保存或回读失败。 |

`get_center_calibration_diagnostics` 的 `result` 可包含 `detailReason`、`phase`、`failure`、`lastReject`、`gateMask`、`feedbackAge`、`encoderAge`、`lastAbort`、CANopen 执行步骤和原始反馈。它们是诊断证据；内部数值阶段与位映射不构成业务控制协议。

## 5. 正常远控与 OTA 边界

正常远控沿用公共 MQTT V4 `chassis_control` 的 `acquire`、`renew`、`stop`，参数为 `sessionId`、递增 `seq`、`leaseMs` 和目标。平台负责 operator/admin 身份、同车独占、目标格式、浏览器失联归零、停止投递与审计；MCU 持续检查标定、CAN、节点、反馈、驱动器、编码器、范围、OTA 和停车条件。重启或 MQTT 重连后须重新 `acquire`。

OTA 沿用内部 `ota_upgrade` 请求及 Boot V2 状态。`INSTALL_PENDING` 仅表示安装已提交；升级完成以设备重新上线、版本、`otaState=CONFIRMED` 和 Config V5 回读共同核对。OTA 响应不证明运动功能已通过台架或现场验收。

## 6. `internal_config` 请求与结果 Schema

每个请求的 `params.action` 必填。下表列出的额外字段仅适用于对应动作；未列出的动作只需要 `action`。参数值采用 JSON 原生类型，不把数字或 Boolean 写为字符串。

| 动作 | 字段 | 类型 | 必填 | 约束 |
| --- | --- | --- | ---: | --- |
| `get_center_calibration`、`jog_center_calibration`、`commit_center_calibration`、`cancel_center_calibration` | `sessionId` | String | 是 | 1～39 字符，原样使用 `start` 返回值。 |
| `jog_center_calibration` | `deltaMdeg` | Integer | 是 | `-5000`、`-1000`、`-200`、`200`、`1000`、`5000`。 |
| `set_mqtt` | `vid` | String | 是 | 1～31 字符；英文字母、数字、`.`、`_`、`-`。 |
| `set_mqtt` | `host` | String | 是 | 1～63 个可打印 ASCII 字符，不含引号或反斜杠。 |
| `set_mqtt` | `port` | Integer | 是 | `1..65535`。 |
| `set_mqtt` | `username` | String | 是 | 1～31 个可打印 ASCII 字符，不含引号或反斜杠。 |
| `set_mqtt` | `password` | String | 是 | 1～63 个可打印 ASCII 字符，不含引号或反斜杠。 |
| `set_mqtt` | `tlsEnabled` | Boolean | 是 | 是否使用 TLS。 |
| `set_mqtt` | `tlsProfileId` | Integer | 是 | `0..255`。 |

### 6.1 查询与诊断

`get_info` 返回当前信息快照。字段按组列出，均在 `result` 中；未读到的 Boot 描述符以 `bootDescriptorValid=false` 和空版本表示，不能把默认数值理解为已确认状态。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `fwVersion`、`bootVersion`、`vid`、`iccid` | String | 固件、Boot、VID 和模组身份。 |
| `bootDescriptorValid`、`otaBootstrapNeedsRevision` | Boolean | Boot 描述符有效性和引导修订提示。 |
| `bootApiVersion`、`bootFeatures`、`resetReason`、`uptimeMs` | Integer | Boot 描述符、重启原因和启动后毫秒数。 |
| `otaScheme`、`otaDownloader`、`stableSeedSource` | String | OTA 与稳定镜像元数据。 |
| `mqttHost`、`mqttState` | String | 已提交的 MQTT 主机和当前连接状态。 |
| `mqttPort`、`mqttTlsProfileId`、`mqttStateCode` | Integer | 已提交的端口、TLS profile 和状态码。 |
| `mqttTls`、`canInitialized`、`canBusOff` | Boolean | 已提交 TLS 选择及 CAN 当前摘要。 |
| `canRxOverflow`、`stableSlot`、`candidateSlot`、`trialFailCount` | Integer | CAN 溢出和 Boot 状态计数。 |
| `otaState`、`otaPhase`、`otaTargetVersion` | String | Boot 状态、当前阶段和目标版本。 |
| `otaProvisioned`、`otaReady`、`otaBusy` | Boolean | OTA 准备及忙碌状态。 |
| `lastOtaError` | Integer | 最近 OTA 错误码。 |
| `powerOutputAvailable`、`powerOutputOn` | Boolean | MCU 输出接口是否存在、输出命令是否置位；均非触点反馈。 |
| `diagnostics` | Object | `canRx: Integer`、`steerHb: Boolean`、`driveHb: Boolean`、`encoderOnline: Boolean`。 |

`get_can_diag` 返回只读快照，数值初始为零或沿用最后一次样本时须结合新鲜度解释。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `canRx`、`can1RawRx`、`can2RawRx`、`canIgnored`、`canRxOverflow` | Integer | CAN 接收与丢弃计数。 |
| `canBusOff`、`canBusOffActive` | Boolean | 本次启动曾发生 Bus-Off、当前仍处于 Bus-Off。 |
| `steerHb`、`driveHb`、`steerFresh`、`driveFresh` | Boolean | 节点在线和运动反馈新鲜度。 |
| `steerNmt`、`driveNmt`、`steerSw`、`driveSw` | Integer | NMT 状态和驱动器状态字。 |
| `steerRaw`、`leftRpm`、`rightRpm` | Integer | 最近转向原始位置和左右原始转速。 |
| `sdoOk`、`sdoReadMask`、`sdoAbortMask`、`sdoTimeouts`、`sdoAborts`、`lastAbort`、`sdoTxErrors` | Integer | SDO 事务、对象位图和故障计数。 |
| `driveObjects` | Array<Integer> | 七项最近读取的行走驱动对象值；顺序由厂商对象诊断定义。 |
| `encoderRaw` | Integer | 独立编码器最近单圈计数。 |
| `encoderOnline`、`encoderConfigValid` | Boolean | 编码器在线与配置有效性。 |
| `encoderLastError`、`encoderTimeouts`、`encoderCrcErrors`、`encoderExceptions`、`encoderFormatErrors`、`encoderTransportErrors` | Integer | 编码器错误与计数。 |

`get_calibration` 返回以下完整对象；`calibrated=true` 仍须结合 `profileCompatible=true` 使用。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `profileCompatible`、`calibrated` | Boolean | 配置与当前转向模型兼容、标定数据已保存。 |
| `configVersion` | Integer | Flash Config 格式，正式路径为 `5`。 |
| `steeringCenterCount` | Integer | 转向驱动器中位原始计数。 |
| `absoluteCenterCount` | Integer | 独立编码器中位单圈计数。 |
| `steeringCountsPerDegreeMilli`、`absoluteCountsPerDegreeMilli` | Integer | 两路角度换算系数。 |
| `steeringDirection`、`absoluteDirection` | Integer | 两路角度方向，`-1` 或 `1`。 |
| `steeringToleranceMdeg` | Integer | 双角度容差，毫度。 |

`get_log` 将快照分片发送到 `/internal/log`；每个分片的 `data` 为 `product: String`、`requestId: String`、`sequence: Integer`（从 1 开始）、`total: Integer`、`lines: Array<String>`。全部分片送出后的配置响应为 `result.lineCount: Integer`、`result.dropped: Integer`；空快照直接返回两项计数。主动告警同 Topic，使用 `data.level` 和 `data.lines`，没有查询用的分片序号。

### 6.2 标定与故障动作

`start_center_calibration`、`get_center_calibration`、`jog_center_calibration`、`commit_center_calibration`、`cancel_center_calibration` 的响应均使用第 4 节五字段高层结果。`start` 返回设备生成的 `sessionId`；`jog` 成功只表示请求已接受，须查询至 `READY`；`commit` 成功先返回 `202 CENTER_CALIBRATION_COMMIT_STARTED` 和 `BUSY`，完成后为 `DONE`；`cancel` 在受控停车期间可返回 `BUSY`，停止完成后为 `IDLE`。`409 CENTER_CALIBRATION_NOT_READY` 的 `result` 仍包含当前高层状态和业务 `reason`。

`get_center_calibration_diagnostics` 返回内部证据对象：顶层 `phase: Integer`、`sessionId: String`、`detailReason: String`、`fault: Integer`、`driverRaw: Integer`、`encoderRaw: Integer`、`targetMdeg: Integer`、`jogOffsetMdeg: Integer`、`profileReady: Boolean`、`rangeValid: Boolean`、`calibrated: Boolean`、`driverRelativeMdeg: Integer`、`encoderRelativeMdeg: Integer`、`relativeValid: Boolean`、`commissioningLimitMdeg: Integer`、`diagVersion: Integer`。`failure` 是首个失败快照，含 `phase`、`reason`、`atMs`、`gateMask`、原始反馈、转速、反馈年龄、编码器错误、转向执行步骤、状态字和 `lastAbort`；`lastReject` 是最近请求拒绝，含 `reason`、`atMs`、`gateMask`。这些字段仅供诊断，不是平台业务条件。

`reset_motion_fault` 只在电源输出命令关闭且 OTA 空闲时接受，成功为 `200 FAULT_RESET_REARM_REQUIRED`，结果固定包含 `powerOutputOn=false`、`faultRecordPreserved=true`；既有控制会话不恢复。拒绝为 `409 POWER_OFF_AND_OTA_IDLE_REQUIRED`。

### 6.3 MQTT 配置与重启

`set_mqtt` 将全部字段作为一次不可拆分的候选配置。旧连接先返回 `202 MQTT_CANDIDATE_TESTING`，结果为 `accepted=true, flashWrite=false`；候选连接、业务证明、Config V5 保存及回读完成后，使用同一 `requestId` 返回 `200 MQTT_CONFIG_SAVED`，结果为 `committed=true`。失败并恢复旧连接后返回 `500 MQTT_CANDIDATE_RESTORED`，结果为 `committed=false, restored=true`。仅最终 `200` 表示新配置成为 active；TLS 失败不自行回落到明文。

`mqtt_runtime_get` 为只读诊断：结果包含 `runtimeState: Integer`、`ready: Boolean`、`tls: Boolean`、`client: Integer`、`context: Integer`、`trialState: Integer`、`trialSession: Integer`，以及 `remote: Object`。`remote` 包含 `sessionActive: Boolean`、`sessionId: String`、`enableRequest: Boolean`、`targetRpm: Integer`、`requestedRpm: Integer`、`steeringTargetMdeg: Integer`、`replayCached: Boolean`。内部阶段数值不构成新业务许可。

`reboot` 成功返回 `200 REBOOTING`，`result={}`；响应发送完成后才重启。须以重新上线、身份和状态回读确认重启结果。

## 7. OTA 请求、响应与兼容边界

OTA 请求使用同一内部信封，`data.command="ota_upgrade"`，发往 `/internal/ota`，响应发往 `/internal/ota/resp`。

| `params` 字段 | 类型 | 必填 | 约束 |
| --- | --- | ---: | --- |
| `url` | String | 是 | HTTPS 制品 URL，最长 320 字符，须通过设备来源、路径和文件名校验。 |
| `version` | String | 是 | `patrol-chassis-` 前缀及点分数字，长度小于 32。 |
| `size` | Integer | 是 | `1..221184` 字节，限 App 分区。 |
| `crc32` | String | 是 | 八位十六进制 CRC32。 |

OTA 可能返回 `200 ALREADY_CONFIRMED`（结果 `otaPhase="CONFIRMED"`）、`200 INSTALL_PENDING`（结果 `fwVersion`、`otaPhase="INSTALL_PENDING"`）、`400/503 OTA_REQUEST_REJECTED` 或 `500 OTA_FAILED`（结果 `errorCode: Integer`），以及 `409 DEVICE_BUSY`。`INSTALL_PENDING` 仅表示候选安装已提交；成功须在设备重新上线后核对目标版本、`otaState=CONFIRMED`、Config V5 标定回读。Boot、OTA、Config V5 的存储兼容保留；旧固件的远控业务流程不属于本合同。

```json
{"version":"2.0","timestamp":1788134400000,"data":{"requestId":"cal-start-1","command":"internal_config","params":{"action":"start_center_calibration"}}}
```

```json
{"version":"2.0","timestamp":1788134400123,"data":{"product":"patrol_vehicle_chassis","requestId":"cal-start-1","code":200,"msg":"OK","result":{"state":"BUSY","sessionId":"c-1000-1","calibrated":false,"reason":"NONE","jogOffsetMdeg":0}}}
```

```json
{"version":"2.0","timestamp":1788134401000,"data":{"requestId":"ota-1","command":"ota_upgrade","params":{"url":"https://example.invalid/firmware/patrol-chassis-0.3.0.bin","version":"patrol-chassis-0.3.0","size":124288,"crc32":"1234ABCD"}}}
```
