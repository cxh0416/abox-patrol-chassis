# A-BOX 巡防车底盘内部运维协议 V2.3

| 项目 | 内容 |
| --- | --- |
| 适用产品 | 巡防车底盘（`patrol_vehicle_chassis`） |
| 文档修订号 | V2.3 |
| 报文版本 | `2.0` |
| 文档属性 | 内部运维协议 |
| 依赖协议 | [A-BOX 巡防车底盘业务协议 V4.0](A-BOX巡防车底盘业务协议-V4.0.md) |

## 目录

- [1. 范围与引用](#1-范围与引用)
- [2. 通信约定与报文结构](#2-通信约定与报文结构)
- [3. 内部动作与接口总览](#3-内部动作与接口总览)
- [4. internal_config 接口定义](#4-internal_config-接口定义)
  - [4.1 get_info：设备与能力信息](#41-get_info设备与能力信息)
  - [4.2 get_log：日志快照与主动告警](#42-get_log日志快照与主动告警)
  - [4.3 get_can_diag：CAN 与反馈诊断](#43-get_can_diagcan-与反馈诊断)
  - [4.4 get_calibration：持久标定查询](#44-get_calibration持久标定查询)
  - [4.5 start_center_calibration](#45-start_center_calibration)
  - [4.6 get_center_calibration](#46-get_center_calibration)
  - [4.7 jog_center_calibration](#47-jog_center_calibration)
  - [4.8 commit_center_calibration](#48-commit_center_calibration)
  - [4.9 cancel_center_calibration](#49-cancel_center_calibration)
  - [4.10 get_center_calibration_diagnostics：内部证据](#410-get_center_calibration_diagnostics内部证据)
  - [4.11 reset_motion_fault：故障复位](#411-reset_motion_fault故障复位)
  - [4.12 set_mqtt：候选连接事务](#412-set_mqtt候选连接事务)
  - [4.13 mqtt_runtime_get：运行状态诊断](#413-mqtt_runtime_get运行状态诊断)
  - [4.14 reboot：受控重启](#414-reboot受控重启)
- [5. ota_upgrade：请求、响应与完成边界](#5-ota_upgrade请求响应与完成边界)
- [6. 交互与状态规则](#6-交互与状态规则)
  - [6.1 标定公共结果、状态与拒绝原因](#61-标定公共结果状态与拒绝原因)
  - [6.2 正常远控与 OTA 边界](#62-正常远控与-ota-边界)
- [7. 错误与兼容性](#7-错误与兼容性)
  - [7.1 通用响应码](#71-通用响应码)

## 1. 范围与引用

本规范定义产品 `patrol_vehicle_chassis` 的内部运维 MQTT 请求、响应和标定接口。公共 MQTT V4 信封、上下电、`chassis_control`、`chassis_status` 仍由[底盘业务协议 V4.0](A-BOX巡防车底盘业务协议-V4.0.md)定义。本规范不改变公共协议版本。

本车没有独立的继电器或接触器状态反馈输入。`powerOutputOn` 仅表示 MCU 电源或使能 GPIO 命令状态。驱动器 CAN 状态、实测转速、转向位置和独立编码器是运动反馈来源；MQTT 响应不单独证明真实运动或停车。

## 2. 通信约定与报文结构

V2.3 是本文档修订号；内部报文顶层 `version` 仍为字符串 `"2.0"`。产品标识固定为 `patrol_vehicle_chassis`，`{deviceId}` 是当前 VID。请求 JSON 最多 1023 字节，单条响应或日志 JSON 小于 1080 字节。Topic 不设置 Retain；设备只订阅本 VID 的精确请求 Topic。

**表 1：内部 Topic、方向与 QoS**

| Topic | 方向 | QoS | 用途 |
| --- | --- | ---: | --- |
| `/zxwl/abox/{deviceId}/internal/config` | 平台 → 设备 | 1 | `internal_config` |
| `/zxwl/abox/{deviceId}/internal/config/resp` | 设备 → 平台 | 1 | 配置、标定和诊断响应 |
| `/zxwl/abox/{deviceId}/internal/ota` | 平台 → 设备 | 1 | `ota_upgrade` |
| `/zxwl/abox/{deviceId}/internal/ota/resp` | 设备 → 平台 | 1 | OTA 响应 |
| `/zxwl/abox/{deviceId}/internal/log` | 设备 → 平台 | 主动 0、查询分片 1 | 日志 |

**表 2：内部报文信封字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
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

接收方按精确 Topic、`product`、`requestId` 关联响应；未知的兼容扩展字段可忽略。响应、日志与 MQTT ACK 不替代实测反馈。

## 3. 内部动作与接口总览

`data.command="internal_config"` 时，`data.params.action` 取下列值。未列出的生产动作返回 `404 UNKNOWN_ACTION`。

**表 3：内部维护动作索引**

| action | 额外请求字段 | 响应或接口语义 | 定义位置 |
| --- | --- | --- | --- |
| `get_info` | 无 | 设备、Boot、MQTT、OTA 状态及 `powerOutputOn` 命令状态 | [第 4.1 节](#41-get_info设备与能力信息) |
| `get_log` | 无 | 日志快照分片发往 `/internal/log`，最终响应给出 `lineCount`、`dropped` | [第 4.2 节](#42-get_log日志快照与主动告警) |
| `get_can_diag` | 无 | CAN、驱动器和编码器诊断快照 | [第 4.3 节](#43-get_can_diagcan-与反馈诊断) |
| `get_calibration` | 无 | Config V5 标定值与 `profileCompatible`、`calibrated` | [第 4.4 节](#44-get_calibration持久标定查询) |
| `start_center_calibration` | 无 | 申请独占标定会话，返回高层标定状态 | [第 4.5 节](#45-start_center_calibration) |
| `get_center_calibration` | `sessionId` | 查询本次标定状态；查询不延长 READY 空闲期限 | [第 4.6 节](#46-get_center_calibration) |
| `jog_center_calibration` | `sessionId`、`deltaMdeg` | 请求一次受限转向点动；步长只可为 `±200`、`±1000`、`±5000` 毫度 | [第 4.7 节](#47-jog_center_calibration) |
| `commit_center_calibration` | `sessionId` | 确认机械中位后开始写入和读回，成功请求返回 `202 CENTER_CALIBRATION_COMMIT_STARTED` | [第 4.8 节](#48-commit_center_calibration) |
| `cancel_center_calibration` | `sessionId` | 请求受控停止并结束标定；执行器停稳前状态可以继续为 BUSY | [第 4.9 节](#49-cancel_center_calibration) |
| `get_center_calibration_diagnostics` | 无 | 按需读取内部阶段和故障证据，不用于平台安全裁决 | [第 4.10 节](#410-get_center_calibration_diagnostics内部证据) |
| `reset_motion_fault` | 无 | 仅下电且 OTA 空闲时复位运动故障；旧控制会话不恢复 | [第 4.11 节](#411-reset_motion_fault故障复位) |
| `set_mqtt` | `vid`、`host`、`port`、`username`、`password`、`tlsEnabled`、`tlsProfileId` | MQTT 候选试连和提交，可能先返回 `202 MQTT_CANDIDATE_TESTING` | [第 4.12 节](#412-set_mqtt候选连接事务) |
| `mqtt_runtime_get` | 无 | 查询当前 MQTT runtime 与候选状态 | [第 4.13 节](#413-mqtt_runtime_get运行状态诊断) |
| `reboot` | 无 | 接受重启请求；须另行观察设备重新上线 | [第 4.14 节](#414-reboot受控重启) |

## 4. `internal_config` 接口定义

每个请求的 `params.action` 必填。各动作表中列出的额外字段仅适用于对应动作；未列出的动作只需要 `action`。参数值采用 JSON 原生类型，不把数字或 Boolean 写为字符串。

本章动作均使用[第 2 节](#2-通信约定与报文结构)的配置 Topic、QoS 与信封；`params.action` 为对应小节名称。通用响应码见[第 7.1 节](#71-通用响应码)。

### 4.1 `get_info`：设备与能力信息

**用途、请求参数与结果**

只读当前快照；除 `action` 外无额外请求字段。

`get_info` 返回当前信息快照。字段按路径逐项列出，均在 `result` 中；未读到的 Boot 描述符以 `bootDescriptorValid=false` 和空版本表示，不能把默认数值理解为已确认状态。

**表 4：get_info 结果字段**

| 字段路径 | 类型 | 约束与说明 |
| --- | --- | --- |
| `result.fwVersion` | String | 当前固件版本。 |
| `result.bootVersion` | String | Boot 版本。 |
| `result.vid` | String | 设备 VID。 |
| `result.iccid` | String | 模组身份 ICCID。 |
| `result.bootDescriptorValid` | Boolean | Boot 描述符是否有效。 |
| `result.otaBootstrapNeedsRevision` | Boolean | OTA 引导修订提示。 |
| `result.bootApiVersion` | Integer | Boot 描述符 ABI/API 版本。 |
| `result.bootFeatures` | Integer | Boot 描述符功能位。 |
| `result.resetReason` | Integer | 重启原因。 |
| `result.uptimeMs` | Integer | 启动后运行时间，毫秒。 |
| `result.otaScheme` | String | OTA 方案元数据。 |
| `result.otaDownloader` | String | OTA 下载执行方元数据。 |
| `result.stableSeedSource` | String | 稳定镜像来源元数据。 |
| `result.maintenanceProtocols` | Object | 事务能力对象，定义与边界见下文。 |
| `result.mqttHost` | String | 已提交的 MQTT 主机。 |
| `result.mqttState` | String | 当前 MQTT 连接状态。 |
| `result.mqttPort` | Integer | 已提交的 MQTT 端口。 |
| `result.mqttTlsProfileId` | Integer | 已提交的 MQTT TLS profile。 |
| `result.mqttStateCode` | Integer | MQTT 状态码。 |
| `result.mqttTls` | Boolean | 已提交的 TLS 选择。 |
| `result.canInitialized` | Boolean | CAN 初始化摘要。 |
| `result.canBusOff` | Boolean | CAN 当前摘要；具体兼容含义见对应诊断定义。 |
| `result.canRxOverflow` | Integer | CAN 接收溢出计数。 |
| `result.stableSlot` | Integer | Boot 稳定槽编号。 |
| `result.candidateSlot` | Integer | Boot 候选槽编号。 |
| `result.trialFailCount` | Integer | Boot 试运行失败计数。 |
| `result.otaState` | String | Boot OTA 状态。 |
| `result.otaPhase` | String | 当前 OTA 阶段。 |
| `result.otaTargetVersion` | String | OTA 目标版本。 |
| `result.otaProvisioned` | Boolean | OTA 准备状态。 |
| `result.otaReady` | Boolean | OTA 就绪状态。 |
| `result.otaBusy` | Boolean | OTA 忙碌状态。 |
| `result.lastOtaError` | Integer | 最近 OTA 错误码。 |
| `result.powerOutputAvailable` | Boolean | MCU 输出接口是否存在；不是触点反馈。 |
| `result.powerOutputOn` | Boolean | MCU 输出命令是否置位；不是触点反馈。 |
| `result.diagnostics` | Object | 子字段见本节独立表。 |

**内部维护事务能力**

`mqttConfigTransaction: 1`、`vidChangeTransaction: 1` 分别声明 MQTT 配置与 VID 的候选试连、保存回读及失败恢复事务语义。协议版本独立于应用固件版本；声明不代表当前允许执行。缺失或未知版本不能推断支持。

**表 5：get_info 事务能力与诊断子字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `result.maintenanceProtocols.mqttConfigTransaction` | 版本值 `1` | 能力声明 | MQTT 配置候选试连、保存回读与失败恢复事务。 |
| `result.maintenanceProtocols.vidChangeTransaction` | 版本值 `1` | 能力声明 | VID 候选试连、保存回读与失败恢复事务。 |
| `result.diagnostics.canRx` | Integer | 诊断对象 | CAN 接收计数。 |
| `result.diagnostics.steerHb` | Boolean | 诊断对象 | 转向节点心跳。 |
| `result.diagnostics.driveHb` | Boolean | 诊断对象 | 行走节点心跳。 |
| `result.diagnostics.encoderOnline` | Boolean | 诊断对象 | 编码器在线。 |

### 4.2 `get_log`：日志快照与主动告警

**用途与请求参数**

除 `action` 外无额外字段。

**结果、分片与执行规则**

`get_log` 将快照分片发送到 `/internal/log`；每个分片的 `data` 为 `product: String`、`requestId: String`、`sequence: Integer`（从 1 开始）、`total: Integer`、`lines: Array<String>`。全部分片送出后的配置响应为 `result.lineCount: Integer`、`result.dropped: Integer`；空快照直接返回两项计数。主动告警同 Topic，使用 `data.level` 和 `data.lines`，没有查询用的分片序号。

**表 6：日志上传与查询结果字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `data.product` | String | 查询日志分片 | 产品标识。 |
| `data.requestId` | String | 查询日志分片 | 关联原请求。 |
| `data.sequence` | Integer | 查询日志分片 | 从 1 开始。 |
| `data.total` | Integer | 查询日志分片 | 分片总数。 |
| `data.lines` | Array&lt;String&gt; | 查询日志分片 / 主动告警 | 日志行。 |
| `result.lineCount` | Integer | 查询最终响应 | 全部分片送出后响应；空快照直接返回。 |
| `result.dropped` | Integer | 查询最终响应 | 全部分片送出后响应；空快照直接返回。 |

### 4.3 `get_can_diag`：CAN 与反馈诊断

**用途、请求参数与结果**

只读快照；除 `action` 外无额外字段。

`get_can_diag` 返回只读快照，数值初始为零或沿用最后一次样本时须结合新鲜度解释。

**表 7：get_can_diag 结果字段**

| 字段路径 | 类型 | 约束与说明 |
| --- | --- | --- |
| `result.canRx` | Integer | CAN 接收计数。 |
| `result.can1RawRx` | Integer | CAN1 原始接收计数。 |
| `result.can2RawRx` | Integer | CAN2 原始接收计数。 |
| `result.canIgnored` | Integer | CAN 丢弃计数。 |
| `result.canRxOverflow` | Integer | CAN 接收溢出计数。 |
| `result.canBusOff` | Boolean | 本次启动曾发生 Bus-Off。 |
| `result.canBusOffActive` | Boolean | 当前仍处于 Bus-Off。 |
| `result.steerHb` | Boolean | 转向节点在线。 |
| `result.driveHb` | Boolean | 行走节点在线。 |
| `result.steerFresh` | Boolean | 转向运动反馈新鲜度。 |
| `result.driveFresh` | Boolean | 行走运动反馈新鲜度。 |
| `result.steerNmt` | Integer | 转向节点 NMT 状态。 |
| `result.driveNmt` | Integer | 行走节点 NMT 状态。 |
| `result.steerSw` | Integer | 转向驱动器状态字。 |
| `result.driveSw` | Integer | 行走驱动器状态字。 |
| `result.steerRaw` | Integer | 最近转向原始位置。 |
| `result.leftRpm` | Integer | 最近左侧原始转速。 |
| `result.rightRpm` | Integer | 最近右侧原始转速。 |
| `result.sdoOk` | Integer | SDO 成功事务诊断。 |
| `result.sdoReadMask` | Integer | SDO 读取对象位图。 |
| `result.sdoAbortMask` | Integer | SDO 终止对象位图。 |
| `result.sdoTimeouts` | Integer | SDO 超时计数。 |
| `result.sdoAborts` | Integer | SDO 终止计数。 |
| `result.lastAbort` | Integer | 最近 SDO 终止证据。 |
| `result.sdoTxErrors` | Integer | SDO 发送错误计数。 |
| `result.driveObjects` | Array&lt;Integer&gt; | 七项最近读取的行走驱动对象值；顺序由厂商对象诊断定义。 |
| `result.encoderRaw` | Integer | 独立编码器最近单圈计数。 |
| `result.encoderOnline` | Boolean | 编码器在线。 |
| `result.encoderConfigValid` | Boolean | 编码器配置有效性。 |
| `result.encoderLastError` | Integer | 最近编码器错误。 |
| `result.encoderTimeouts` | Integer | 编码器超时计数。 |
| `result.encoderCrcErrors` | Integer | 编码器 CRC 错误计数。 |
| `result.encoderExceptions` | Integer | 编码器异常计数。 |
| `result.encoderFormatErrors` | Integer | 编码器格式错误计数。 |
| `result.encoderTransportErrors` | Integer | 编码器传输错误计数。 |

### 4.4 `get_calibration`：持久标定查询

**用途、请求参数与结果**

除 `action` 外无额外字段。

`get_calibration` 返回以下完整对象；`calibrated=true` 仍须结合 `profileCompatible=true` 使用。

**表 8：get_calibration 结果字段**

| 字段路径 | 类型 | 约束与说明 |
| --- | --- | --- |
| `result.profileCompatible` | Boolean | 配置与当前转向模型兼容。 |
| `result.calibrated` | Boolean | 标定数据已保存。 |
| `result.configVersion` | Integer | Flash Config 格式，正式路径为 `5`。 |
| `result.steeringCenterCount` | Integer | 转向驱动器中位原始计数。 |
| `result.absoluteCenterCount` | Integer | 独立编码器中位单圈计数。 |
| `result.steeringCountsPerDegreeMilli` | Integer | 转向驱动器角度换算系数。 |
| `result.absoluteCountsPerDegreeMilli` | Integer | 独立编码器角度换算系数。 |
| `result.steeringDirection` | Integer | 转向驱动器角度方向，`-1` 或 `1`。 |
| `result.absoluteDirection` | Integer | 独立编码器角度方向，`-1` 或 `1`。 |
| `result.steeringToleranceMdeg` | Integer | 双角度容差，毫度。 |

### 4.5 `start_center_calibration`

**用途与请求参数**

申请独占标定会话；除 `action` 外无额外字段。

**响应结果、特殊错误及执行规则**

`start` 返回设备生成的 `sessionId`；

高层结果字段、状态期限及拒绝原因的唯一公共定义见[第 6.1 节](#61-标定公共结果状态与拒绝原因)。标定拒绝 `409 CENTER_CALIBRATION_NOT_READY` 的结果保留当前高层状态和 `reason`。

**请求和响应示例**

**示例 1：`start_center_calibration`（平台 → 设备；完整请求）**

```json
{
  "version": "2.0",
  "timestamp": 0,
  "data": {
    "requestId": "cal-1",
    "command": "internal_config",
    "params": {
      "action": "start_center_calibration"
    }
  }
}
```

**示例 2：`start_center_calibration`（设备 → 平台；完整响应）**

```json
{
  "version": "2.0",
  "timestamp": 0,
  "data": {
    "product": "patrol_vehicle_chassis",
    "requestId": "cal-1",
    "code": 200,
    "msg": "OK",
    "result": {
      "state": "BUSY",
      "sessionId": "c-1-1",
      "calibrated": false,
      "reason": "NONE",
      "jogOffsetMdeg": 0
    }
  }
}
```

**示例 3：`start_center_calibration`（平台 → 设备；完整请求）**

```json
{
  "version": "2.0",
  "timestamp": 1788134400000,
  "data": {
    "requestId": "cal-start-1",
    "command": "internal_config",
    "params": {
      "action": "start_center_calibration"
    }
  }
}
```

**示例 4：`start_center_calibration`（设备 → 平台；完整响应）**

```json
{
  "version": "2.0",
  "timestamp": 1788134400123,
  "data": {
    "product": "patrol_vehicle_chassis",
    "requestId": "cal-start-1",
    "code": 200,
    "msg": "OK",
    "result": {
      "state": "BUSY",
      "sessionId": "c-1000-1",
      "calibrated": false,
      "reason": "NONE",
      "jogOffsetMdeg": 0
    }
  }
}
```

### 4.6 `get_center_calibration`

**用途与请求参数**

查询本次标定状态。

**表 9：get_center_calibration 请求参数**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `params.sessionId` | String | 必填 | 1～39 字符，原样使用 `start` 返回值。 |

**响应结果、特殊错误及执行规则**

查询本次标定状态；查询不延长 READY 空闲期限。

高层结果字段、状态期限及拒绝原因的唯一公共定义见[第 6.1 节](#61-标定公共结果状态与拒绝原因)。标定拒绝 `409 CENTER_CALIBRATION_NOT_READY` 的结果保留当前高层状态和 `reason`。

### 4.7 `jog_center_calibration`

**用途与请求参数**

请求一次受限转向点动。

**表 10：jog_center_calibration 请求参数**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `params.sessionId` | String | 必填 | 1～39 字符，原样使用 `start` 返回值。 |
| `params.deltaMdeg` | Integer | 必填 | `-5000`、`-1000`、`-200`、`200`、`1000`、`5000`；点动步长只可为 `±200`、`±1000`、`±5000` 毫度。 |

**响应结果、特殊错误及执行规则**

`jog` 成功只表示请求已接受，须查询至 `READY`；

高层结果字段、状态期限及拒绝原因的唯一公共定义见[第 6.1 节](#61-标定公共结果状态与拒绝原因)。标定拒绝 `409 CENTER_CALIBRATION_NOT_READY` 的结果保留当前高层状态和 `reason`。

### 4.8 `commit_center_calibration`

**用途与请求参数**

确认机械中位后开始写入和读回。

**表 11：commit_center_calibration 请求参数**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `params.sessionId` | String | 必填 | 1～39 字符，原样使用 `start` 返回值。 |

**响应结果、特殊错误及执行规则**

`commit` 成功先返回 `202 CENTER_CALIBRATION_COMMIT_STARTED` 和 `BUSY`，完成后为 `DONE`；

高层结果字段、状态期限及拒绝原因的唯一公共定义见[第 6.1 节](#61-标定公共结果状态与拒绝原因)。标定拒绝 `409 CENTER_CALIBRATION_NOT_READY` 的结果保留当前高层状态和 `reason`。

### 4.9 `cancel_center_calibration`

**用途与请求参数**

请求受控停止并结束标定；执行器停稳前状态可以继续为 BUSY。

**表 12：cancel_center_calibration 请求参数**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `params.sessionId` | String | 必填 | 1～39 字符，原样使用 `start` 返回值。 |

**响应结果、特殊错误及执行规则**

`cancel` 在受控停车期间可返回 `BUSY`，停止完成后为 `IDLE`。

高层结果字段、状态期限及拒绝原因的唯一公共定义见[第 6.1 节](#61-标定公共结果状态与拒绝原因)。标定拒绝 `409 CENTER_CALIBRATION_NOT_READY` 的结果保留当前高层状态和 `reason`。

### 4.10 `get_center_calibration_diagnostics`：内部证据

除 `action` 外无额外字段。

`get_center_calibration_diagnostics` 的 `result` 可包含 `detailReason`、`phase`、`failure`、`lastReject`、`gateMask`、`feedbackAge`、`encoderAge`、`lastAbort`、CANopen 执行步骤和原始反馈。它们是诊断证据；内部数值阶段与位映射不构成业务控制协议。

`get_center_calibration_diagnostics` 返回内部证据对象：顶层字段见下表。

**表 13：标定诊断顶层结果字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `result.phase` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.sessionId` | String | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.detailReason` | String | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.fault` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.driverRaw` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.encoderRaw` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.targetMdeg` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.jogOffsetMdeg` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.profileReady` | Boolean | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.rangeValid` | Boolean | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.calibrated` | Boolean | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.driverRelativeMdeg` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.encoderRelativeMdeg` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.relativeValid` | Boolean | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.commissioningLimitMdeg` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |
| `result.diagVersion` | Integer | 诊断顶层 | 内部证据，不是平台业务条件。 |

**失败快照与最近拒绝**

`failure` 是首个失败快照，含 `phase`、`reason`、`atMs`、`gateMask`、原始反馈、转速、反馈年龄、编码器错误、转向执行步骤、状态字和 `lastAbort`；`lastReject` 是最近请求拒绝，含 `reason`、`atMs`、`gateMask`。这些字段仅供诊断，不是平台业务条件。

**表 14：标定诊断失败快照与最近拒绝**

| 字段路径 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- |
| `result.failure` | 首个失败 | 字段及原始反馈组见上文。 |
| `result.failure.phase` | 失败快照 | 内部阶段。 |
| `result.failure.reason` | 失败快照 | 失败原因。 |
| `result.failure.atMs` | 失败快照 | 记录时间。 |
| `result.failure.gateMask` | 失败快照 | 诊断门禁位。 |
| `result.failure.lastAbort` | 失败快照 | CANopen 终止证据。 |
| `result.lastReject` | 最近请求拒绝 | 含 reason、atMs、gateMask。 |
| `result.lastReject.reason` | 最近拒绝 | 拒绝原因。 |
| `result.lastReject.atMs` | 最近拒绝 | 记录时间。 |
| `result.lastReject.gateMask` | 最近拒绝 | 诊断门禁位。 |

### 4.11 `reset_motion_fault`：故障复位

除 `action` 外无额外请求字段。

**结果、错误与执行条件**

`reset_motion_fault` 只在电源输出命令关闭且 OTA 空闲时接受，成功为 `200 FAULT_RESET_REARM_REQUIRED`，结果固定包含 `powerOutputOn=false`、`faultRecordPreserved=true`；既有控制会话不恢复。拒绝为 `409 POWER_OFF_AND_OTA_IDLE_REQUIRED`。

**表 15：reset_motion_fault 响应结果**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `result.powerOutputOn` | Boolean | 成功结果固定包含 | `false`。 |
| `result.faultRecordPreserved` | Boolean | 成功结果固定包含 | `true`。 |

### 4.12 `set_mqtt`：候选连接事务

**请求参数**

**表 16：set_mqtt 请求参数**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `params.vid` | String | 是 | 1～31 个 ASCII 字符；仅英文字母、数字、`_`、`-`，不允许点号；与公共 deviceId 使用相同字符集。 |
| `params.host` | String | 是 | 1～63 个可打印 ASCII 字符，不含引号或反斜杠。 |
| `params.port` | Integer | 是 | `1..65535`。 |
| `params.username` | String | 是 | 1～31 个可打印 ASCII 字符，不含引号或反斜杠。 |
| `params.password` | String | 是 | 1～63 个可打印 ASCII 字符，不含引号或反斜杠。 |
| `params.tlsEnabled` | Boolean | 是 | 是否使用 TLS。 |
| `params.tlsProfileId` | Integer | 是 | `0..255`。 |

**响应结果、特殊错误及执行规则**

`set_mqtt` 将全部字段作为一次不可拆分的候选配置。旧连接先返回 `202 MQTT_CANDIDATE_TESTING`，结果为 `accepted=true, flashWrite=false`；候选连接、业务证明、Config V5 保存及回读完成后，使用同一 `requestId` 返回 `200 MQTT_CONFIG_SAVED`，结果为 `committed=true`。失败并恢复旧连接后返回 `500 MQTT_CANDIDATE_RESTORED`，结果为 `committed=false, restored=true`。仅最终 `200` 表示新配置成为 active；TLS 失败不自行回落到明文。

候选事务超时为 120 秒，恢复连接超时为 90 秒。窗口用于安排查询和超时处理，不能仅凭经过时间判定已提交或已恢复。

**连接、身份与最终回执**

| 阶段 | Broker / 身份 | 回执或完成依据 |
| --- | --- | --- |
| 受理 | 旧 Broker、旧 VID | 旧 `/zxwl/abox/{oldVid}/internal/config/resp` 返回同一 requestId 的 `202` |
| 候选试连 | 候选 Broker、候选 VID；ClientId 等于候选 VID | 使用候选 VID 的精确 Topic；此时尚不能认为配置已持久化 |
| 保存与回读成功 | 候选 Broker、候选 VID | 新 `/zxwl/abox/{newVid}/internal/config/resp` 返回同一 requestId 的 `200`；仅修改 Broker 时 VID 保持不变 |
| 失败并恢复连接 | 旧 Broker、旧 VID | 旧响应 Topic 返回同一 requestId 的 `500`；旧连接仍不可用时平台可能收不到最终回执 |

平台发起前应具备旧、新端点及精确 Topic 的访问权限，并先订阅两侧回执。跨 Broker 时须保持各自的订阅连接。最终回执未收到只能记为结果未知，不能将 `202`、新身份上线或超时直接解释为保存成功或已恢复。

结果未知时，待事务结束或设备恢复上线后，在可连接的旧、新身份下以新 requestId 查询 `get_info`，核对实际返回的 `vid`、`mqttHost`、`mqttPort`、`mqttTls`、`mqttTlsProfileId`，并结合 `mqtt_runtime_get.trialState` 判断候选是否结束。候选期内看到新 Topic 不证明持久化。读取与候选一致的已保存配置且候选已结束，才可据此对账为提交；读取旧配置则对账为恢复。仅修改密码、账号或 PSK 且新旧回显字段相同时，不能仅凭上述字段判断结果；应结合受信 Broker 的已认证账号及凭据代次证据，证据仍不能区分提交与恢复时保持未知。未回显的字段不视为已经验证。两侧均不可查询时保持未知。

不把相同 requestId 作为跨重启的持久事务查询接口。收到最终结果前不并发启动第二笔配置事务；先查明当前配置，再以新 requestId 提交下一笔变更。凭据不进入响应、日志或对账记录。

**表 17：set_mqtt 事务响应结果**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `result.accepted` | Boolean | 202 候选受理 | `true`。 |
| `result.flashWrite` | Boolean | 202 候选受理 | `false`。 |
| `result.committed` | Boolean | 最终响应 | 成功为 true；失败恢复为 false。 |
| `result.restored` | Boolean | 500 失败恢复 | `true`。 |

### 4.13 `mqtt_runtime_get`：运行状态诊断

除 `action` 外无额外请求字段。

`mqtt_runtime_get` 为只读诊断：结果包含 `runtimeState: Integer`、`ready: Boolean`、`tls: Boolean`、`client: Integer`、`context: Integer`、`trialState: Integer`、`trialSession: Integer`，以及 `remote: Object`。`remote` 包含 `sessionActive: Boolean`、`sessionId: String`、`enableRequest: Boolean`、`targetRpm: Integer`、`requestedRpm: Integer`、`steeringTargetMdeg: Integer`、`replayCached: Boolean`。内部阶段数值不构成新业务许可。

**表 18：mqtt_runtime_get 结果字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
| --- | --- | --- | --- |
| `result.runtimeState` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.ready` | Boolean | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.tls` | Boolean | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.client` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.context` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.trialState` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.trialSession` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote` | Object | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.sessionActive` | Boolean | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.sessionId` | String | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.enableRequest` | Boolean | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.targetRpm` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.requestedRpm` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.steeringTargetMdeg` | Integer | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |
| `result.remote.replayCached` | Boolean | 只读结果 | 诊断状态；内部阶段数值不构成新业务许可。 |

`trialState` 为只读进度诊断：0 IDLE；1 WAIT_ACCEPT；2 PREPARING；3 CONNECTING；4 SUBSCRIBING；5 VERIFYING；6 COMMITTING；7 RESTORING；8 COMMITTED；9 FAILED；10 RESTORE_FAILED。8 表示事务已保存回读，9 表示失败并已恢复，10 表示恢复失败；未知值仅展示为未知进度。该字段不构成新的执行授权，重启后的 IDLE 也不能独自证明上一笔事务结果，仍须查询已保存配置对账。

### 4.14 `reboot`：受控重启

除 `action` 外无额外请求字段。

`reboot` 成功返回 `200 REBOOTING`，`result={}`；响应发送完成后才重启。须以重新上线、身份和状态回读确认重启结果。

## 5. `ota_upgrade`：请求、响应与完成边界

OTA 请求使用同一内部信封，`data.command="ota_upgrade"`，发往 `/internal/ota`，响应发往 `/internal/ota/resp`。

**表 19：ota_upgrade 请求参数**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `params.url` | String | 是 | HTTPS 制品 URL，最长 320 字符，须通过设备来源、路径和文件名校验。 |
| `params.version` | String | 是 | `patrol-chassis-` 前缀及点分数字，长度小于 32。 |
| `params.size` | Integer | 是 | `1..221184` 字节，限 App 分区。 |
| `params.crc32` | String | 是 | 八位十六进制 CRC32。 |

OTA 可能返回 `200 ALREADY_CONFIRMED`（结果 `otaPhase="CONFIRMED"`）、`200 INSTALL_PENDING`（结果 `fwVersion`、`otaPhase="INSTALL_PENDING"`）、`400/503 OTA_REQUEST_REJECTED` 或 `500 OTA_FAILED`（结果 `errorCode: Integer`），以及 `409 DEVICE_BUSY`。`INSTALL_PENDING` 仅表示候选安装已提交；成功须在设备重新上线后核对目标版本、`otaState=CONFIRMED`、Config V5 标定回读。Boot、OTA、Config V5 的存储兼容保留；旧固件的远控业务流程不属于本合同。

**请求示例（占位制品地址）**

**示例 5：`ota_upgrade`：请求、响应与完成边界（平台 → 设备；完整请求）**

```json
{
  "version": "2.0",
  "timestamp": 1788134401000,
  "data": {
    "requestId": "ota-1",
    "command": "ota_upgrade",
    "params": {
      "url": "https://example.invalid/firmware/patrol-chassis-0.3.0.bin",
      "version": "patrol-chassis-0.3.0",
      "size": 124288,
      "crc32": "1234ABCD"
    }
  }
}
```

## 6. 交互与状态规则

### 6.1 标定公共结果、状态与拒绝原因

标定请求的 `result` 为以下高层对象；同一会话的 ID 必须原样传回。`jogOffsetMdeg` 表示设备已接受的累计点动目标，不表示实测角度。

**表 20：标定动作共用结果字段**

| 字段路径 | 类型 | 约束与说明 |
| --- | --- | --- |
| `result.state` | String | `IDLE`、`READY`、`BUSY`、`DONE`、`ERROR` |
| `result.sessionId` | String | 当前设备标定会话 ID；空串表示无会话 |
| `result.calibrated` | Boolean | 兼容标定是否可用 |
| `result.reason` | String | 下表稳定业务原因 |
| `result.jogOffsetMdeg` | Integer | 已接受的累计点动目标，毫度 |

**期限与状态转换**

`READY` 自最后一次进入该状态起有 10 分钟纯空闲期限；读取状态不续期。到期返回 `state=IDLE, reason=EXPIRED`，结束会话并关闭电源输出命令，不生成设备故障或故障记录。`DONE` 表示本次提交已完成编码器验证、Config V5 保存及回读；它只供本次事务短暂观察，随后回到 `IDLE, calibrated=true`。设备重启只恢复持久 `calibrated`，不恢复 `DONE`。平台错过 `DONE` 时可调用 `get_calibration` 核对持久标定。

**拒绝与执行故障**

请求拒绝仍可返回当前状态和 `reason`。`ERROR` 表示设备执行故障、持续安全异常或受控停止失败，详情需按需查询诊断。平台不得根据内部阶段、反馈年龄或门禁位放行运动。

**表 21：标定 reason 枚举**

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

`start_center_calibration`、`get_center_calibration`、`jog_center_calibration`、`commit_center_calibration`、`cancel_center_calibration` 的响应均使用[第 6.1 节](#61-标定公共结果状态与拒绝原因)五字段高层结果。`409 CENTER_CALIBRATION_NOT_READY` 的 `result` 仍包含当前高层状态和业务 `reason`。

标定写入只能通过 `start → READY → jog → READY → commit → DONE`。生产接口没有单独的行走或转向许可动作。实验性 RAM MQTT 候选和取消动作不属于本合同；正式写入使用 `set_mqtt`。

### 6.2 正常远控与 OTA 边界

正常远控沿用公共 MQTT V4 `chassis_control` 的 `acquire`、`renew`、`stop`，参数为 `sessionId`、递增 `seq`、`leaseMs` 和目标。平台负责 operator/admin 身份、同车独占、目标格式、浏览器失联归零、停止投递与审计；MCU 持续检查标定、CAN、节点、反馈、驱动器、编码器、范围、OTA 和停车条件。重启或 MQTT 重连后须重新 `acquire`。

OTA 沿用内部 `ota_upgrade` 请求及 Boot V2 状态。`INSTALL_PENDING` 仅表示安装已提交；升级完成以设备重新上线、版本、`otaState=CONFIRMED` 和 Config V5 回读共同核对。OTA 响应不证明运动功能已通过台架或现场验收。

## 7. 错误与兼容性

### 7.1 通用响应码

**表 22：通用响应码与机器消息**

| `code` / `msg` | 接口语义 |
| --- | --- |
| `200 OK` | 查询成功或同步请求已接受；不证明物理动作完成。 |
| `202 CENTER_CALIBRATION_COMMIT_STARTED`、`202 MQTT_CANDIDATE_TESTING` | 异步过程已开始，须继续查询最终结果。 |
| `400 INVALID_REQUEST` | 信封或字段格式不符。 |
| `404 UNKNOWN_ACTION` | 生产动作不存在。 |
| `409 CENTER_CALIBRATION_NOT_READY`、`409 DEVICE_BUSY` | 当前状态拒绝操作或设备正在处理其他操作。标定拒绝优先读取 `result.reason`。 |
| `413 PAYLOAD_TOO_LARGE` | 请求超过 1023 字节。 |
| `500`、`503` | 执行、编码或设备能力错误，以 `msg`、`result` 为准。 |
