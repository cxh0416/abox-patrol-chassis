# A-BOX 车辆底盘业务协议 V3

协议版本：3.0

文档修订：1.6

设备类型：`vehicle_chassis`

## 1. 协议范围

本文定义平台与车辆底盘 A-BOX 之间的公共业务接口，适用于具备行走、转向和运动安全仲裁能力的车辆底盘。

报文外层、UTC 毫秒时间戳、QoS、请求幂等和公共响应码遵循《A-BOX 设备公共 MQTT 协议 V3》。运维接口、内部总线、硬件映射、控制算法和验收记录不属于本协议。

公共 V3 报文统一使用 `data.deviceType=vehicle_chassis`，不得携带内部产品标识。
接入方只能依赖本文明确列出的字段，并应忽略未识别的扩展字段。

### 1.1 公共信封字段

除特别说明外，请求、响应和状态均使用以下信封；业务字段位于 `data` 内。

| 字段 | 类型 | 含义与约束 |
|---|---|---|
| `version` | String | 协议版本，固定为 `3.0`。不匹配时设备不按本 Profile 处理。 |
| `timestamp` | Integer | 发送方生成的 UTC Unix 毫秒时间戳。用于排序和观测，不能替代 `requestId` 幂等或设备反馈时效。 |
| `data` | Object | 本 Profile 的业务负载容器；不增加 `result` 等额外包装层。 |
| `data.deviceType` | String | 设备业务类型，固定为 `vehicle_chassis`。请求不匹配返回 `4004`。 |
| `data.requestId` | String | 仅请求和响应携带。由平台生成；重试必须沿用原值和完全相同的请求内容。 |
| `data.reportId` | String | 仅主动状态携带。用于报告去重，不得用作控制会话标识。 |

## 2. 公共 Topic

`{deviceId}` 与设备当前 VID 为同一值。

| 方向 | 用途 | Topic | QoS | Retain |
|---|---|---|---:|---:|
| 平台 -> A-BOX | 底盘控制 | `/zxwl/abox/{deviceId}/ctrl` | 1 | 否 |
| A-BOX -> 平台 | 控制响应 | `/zxwl/abox/{deviceId}/ctrl/resp` | 1 | 否 |
| A-BOX -> 平台 | 主动状态 | `/zxwl/abox/{deviceId}/status/report` | 周期 0，事件 1 | 是 |
| A-BOX -> 平台 | 公共心跳 | `/zxwl/abox/{deviceId}/heartbeat` | 0 | 否 |

本 Profile 不定义公共状态查询 Topic。平台通过主动状态报告维护最新底盘状态；Retain 状态不代替心跳在线判定。

## 3. 公共心跳

本产品 Profile 的心跳在闲置时每 60 秒发送一次，控制、标定或短时诊断期间每 5 秒发送一次。平台在线超时至少为 180 秒；通信在线不代替运动反馈新鲜度检查。字段只包含公共协议规定的设备类型和当前 App 固件版本：

```json
{
  "version": "3.0",
  "timestamp": 1788134400000,
  "data": {
    "deviceType": "vehicle_chassis",
    "firmware": "1.0.0"
  }
}
```

心跳只表示 A-BOX 与 Broker 的通信存活，不表示下游节点、执行器、电源、机械动作或 OTA 正常。

## 4. 车辆电源控制

### 4.1 请求

```json
{
  "version": "3.0",
  "timestamp": 1788134400000,
  "data": {
    "deviceType": "vehicle_chassis",
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "command": "power_on",
    "params": null
  }
}
```

| 字段 | 类型 | 必填 | 约束 |
|---|---|---:|---|
| `data.deviceType` | String | 是 | 固定为 `vehicle_chassis` |
| `data.requestId` | String | 是 | 平台生成的全局唯一请求标识；重试沿用原值 |
| `data.command` | String | 是 | `power_on` 或 `power_off` |
| `data.params` | null | 是 | 固定为 `null` |

`power_on` 请求车辆电源控制输出开启，`power_off` 请求车辆电源控制输出关闭。重复请求按公共协议的 `requestId` 幂等规则处理。上下电不授予运动许可，不建立或恢复控制会话；下电同时撤销当前运动许可和目标。

### 4.2 响应

```json
{
  "version": "3.0",
  "timestamp": 1788134400123,
  "data": {
    "deviceType": "vehicle_chassis",
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "code": 200,
    "msg": "operation accepted",
    "powerOutputOn": true
  }
}
```

`powerOutputOn` 只表示 A-BOX 的 MCU 输出命令锁存状态。`code=200` 只表示该输出命令已被接受并完成锁存校验，不表示继电器触点、接触器反馈、负载端电压或整车真实上电/下电已经确认。实际供电结果必须由产品具备的独立反馈或现场测量确认。

| 字段 | 类型 | 含义 |
|---|---|---|
| `data.requestId` | String | 与对应请求相同的标识。 |
| `data.code` | Integer | 本次请求的处理结果，含义见下表。 |
| `data.msg` | String | 面向诊断的简短结果说明；平台必须以 `code` 判定结果，不得解析此字段控制流程。 |
| `data.powerOutputOn` | Boolean | 当前 MCU 电源输出命令锁存状态。`true` 为已请求输出开启，`false` 为已请求关闭；均不代表真实车辆供电反馈。 |

| 响应码 | 含义 |
|---:|---|
| `200` | 输出命令已接受 |
| `4001` | JSON、通用字段或参数非法 |
| `4002` | 不支持的命令 |
| `4004` | `deviceType` 不匹配 |
| `5001` | 电源输出不可用或写入校验失败 |

## 5. 底盘控制

### 5.1 请求

```json
{
  "version": "3.0",
  "timestamp": 1788134400000,
  "data": {
    "deviceType": "vehicle_chassis",
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "command": "chassis_control",
    "params": {
      "action": "acquire",
      "sessionId": "9bfb4871-0281-44dd-a3bf-a03e696867a2",
      "seq": 1,
      "leaseMs": 1000,
      "driveRpm": 50,
      "steeringDeg": 1.0
    }
  }
}
```

| 字段 | 类型 | 必填 | 含义与约束 |
|---|---|---:|---|
| `data.deviceType` | String | 是 | 请求所适用的设备业务类型，固定为 `vehicle_chassis`；不匹配返回 `4004`。 |
| `data.requestId` | String | 是 | 本次控制请求的全局唯一标识，用于幂等重试和响应关联；重试必须沿用原值和完全相同的请求内容。 |
| `data.command` | String | 是 | 固定为 `chassis_control`，用于激活、续期或停止底盘控制会话。 |
| `data.params` | Object | 是 | `chassis_control` 的参数容器，必须包含本表其余参数字段。 |
| `data.params.action` | String | 是 | `acquire` 使用已授权会话建立控制租约；`renew` 更新目标并续租；`stop` 请求零速并释放会话。 |
| `data.params.sessionId` | String | 是 | 当前已授权的控制会话标识，由授权方签发或转交，平台不得自行构造。`acquire`、`renew` 和 `stop` 应使用同一标识；会话不匹配返回 `4003`。 |
| `data.params.seq` | Integer | 是 | 会话内的命令序号，用于拒绝乱序或重放。对 `acquire`/`renew` 必须相对当前会话严格递增，范围 `1..4294967295`，不得使用小数。 |
| `data.params.leaseMs` | Integer | 是 | 本次成功 `acquire`/`renew` 后目标可保持的最长时间，单位 ms；范围 `1..1000`，不得使用小数。到期前未成功续租，设备安全停止；`stop` 仍必须携带该字段。 |
| `data.params.driveRpm` | Integer | 是 | 期望的行走转速目标，单位 rpm。正负方向由产品标定定义，`0` 表示零速目标；范围 `-200..200`，不得使用小数。 |
| `data.params.steeringDeg` | Number | 是 | 期望的轮胎转向角目标，单位 °。正负方向由产品标定定义，`0` 表示回中目标；范围 `-30.000..30.000`，精度为 `0.001`°（毫度）。 |

`±200 rpm` 和 `±30.000°` 是协议绝对上限；请求还不得超过当前会话获授的行走和转向幅度。未获行走许可时 `driveRpm` 必须为 `0`。租约到期前未成功续租，设备将目标置零并释放会话。
本协议不定义会话授权的签发接口。

### 5.2 响应

```json
{
  "version": "3.0",
  "timestamp": 1788134400123,
  "data": {
    "deviceType": "vehicle_chassis",
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "code": 200,
    "msg": "operation accepted",
    "targetDriveRpm": 50,
    "targetSteeringDeg": 1.0,
    "stopReason": 0
  }
}
```

响应必须使用公共 V3 的数值 `code` 和字符串 `msg`，不得使用 `commandAccepted` 代替公共响应码，也不得增加 `result` 包装。底盘专用结果字段直接放在 `data` 下。

`code=200` 只表示请求通过格式、会话和安全仲裁并更新了设备目标，不表示车辆已经运动、达到转速或达到转角。平台必须结合后续状态报告中的实测反馈判断实际效果。

| 字段 | 类型 | 含义 |
|---|---|---|
| `data.requestId` | String | 与对应请求相同的标识。 |
| `data.code` | Integer | 本次请求的处理结果，含义见下表。 |
| `data.msg` | String | 面向诊断的简短结果说明；平台必须以 `code` 判定结果。 |
| `data.targetDriveRpm` | Integer | 请求处理后设备当前仲裁的行走目标，范围 `-200..200` rpm；为 `0` 不代表左右车轮已静止。 |
| `data.targetSteeringDeg` | Number | 请求处理后设备当前仲裁的转向目标，范围 `-30.000..30.000`°、精度 `0.001`°；不是实测转角。 |
| `data.stopReason` | Integer | 当前或最近一次安全停止原因码。`0` 表示无停止原因；其他取值见“停止原因码”。 |

| 响应码 | 含义 |
|---:|---|
| `200` | 请求已被设备仲裁接受 |
| `400` | 请求合法，但未获会话授权、超过授权幅度或当前安全条件不允许执行 |
| `4001` | JSON、通用字段、参数或序号非法 |
| `4002` | 不支持的命令或动作 |
| `4003` | 已被其他会话占用或控制流程繁忙 |
| `4004` | `deviceType` 不匹配 |
| `5001` | A-BOX 内部异常，未接受目标 |

### 5.3 停止原因码

`stopReason` 反映设备安全仲裁或执行流程已采用的停止原因；它不证明车辆机械部分已经停止。出现非零值后，平台应等待新的状态报告核对实测转速和安全字段。

| 值 | 名称 | 对外含义 |
|---:|---|---|
| `0` | `none` | 当前没有安全停止原因。 |
| `1` | `remote_disabled` | 当前未获远程运动许可。 |
| `2` | `uncalibrated` | 未具备可用的转向标定。 |
| `3` | `feedback_stale` | 运动反馈已超过有效时限。 |
| `4` | `angle_mismatch` | 两路转角反馈偏差超过允许值。 |
| `5` | `node_offline` | 必需的转向或行走节点离线。 |
| `6` | `can_bus_off` | 底盘 CAN 检测到 Bus-Off。 |
| `7` | `drive_fault` | 转向或行走驱动器报告故障。 |
| `8` | `ota_busy` | OTA 正在进行，禁止运动。 |
| `9` | `power_not_ready` | 当前不满足接受运动目标的电源条件。 |
| `10` | `lease_expired` | 控制租约已到期。 |
| `11` | `direction_timeout` | 换向前静止确认超时，设备已锁存故障。 |
| `12` | `explicit_stop` | 平台发送了 `stop`，或设备执行了明确停止。 |
| `13` | `execution_fault` | 执行流程故障已锁存。 |
| `14` | `encoder_range` | 独立转角反馈不在允许运行范围。 |
| `15` | `encoder_profile` | 保存的转向安全配置不满足运行要求。 |

## 6. 主动状态

```json
{
  "version": "3.0",
  "timestamp": 1788134401000,
  "data": {
    "deviceType": "vehicle_chassis",
    "reportId": "PV000001-1788134401000-17",
    "reportReason": "periodic",
    "motionState": "stopped",
    "targetDriveRpm": 0,
    "targetSteeringDeg": 0.0,
    "leftActualRpm": 0,
    "rightActualRpm": 0,
    "steeringActualDeg": 0.0,
    "absoluteActualDeg": 0.0,
    "steeringOnline": true,
    "driveOnline": true,
    "encoderOnline": true,
    "canBusOff": false,
    "driveFault": false,
    "feedbackFresh": true,
    "calibrated": true,
    "powerReady": true,
    "powerOutputOn": true,
    "otaBusy": false,
    "safetyReady": true,
    "steeringProfileReady": true,
    "steeringRangeValid": true,
    "stopReason": 0,
    "faultLatched": false,
    "steeringBoundary": 0
  }
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `reportId` | String | 设备内唯一报告标识，平台据此去重。 |
| `reportReason` | String | 本报告的触发原因：`periodic` 周期上报、`state_changed` 状态变化、`fault_raised` 新故障、`fault_cleared` 故障解除、`safety_stop` 安全停止。 |
| `motionState` | String | `fault` 表示锁存故障；`reversing_wait` 表示换向前静止确认；`driving` 表示反馈有效且任一侧实测转速绝对值大于 `20 rpm`；其他为 `stopped`。`stopped` 在 `feedbackFresh=false` 时不能证明车轮已静止。 |
| `targetDriveRpm` | Integer | 当前仲裁后的驱动目标，范围 `-200..200` rpm；`0` 不代表左右车轮已静止。 |
| `targetSteeringDeg` | Number | 当前仲裁后的转向目标，范围 `-30.000..30.000`°，精度 `0.001`°；不是实测转角。 |
| `leftActualRpm` / `rightActualRpm` | Integer | 左右驱动的最近实测转速，单位 rpm。必须同时结合 `feedbackFresh` 使用，不以目标上限推定实测绝对值范围。 |
| `steeringActualDeg` | Number | 转向驱动器的最近反馈角度，单位 °；必须结合 `feedbackFresh` 使用。 |
| `absoluteActualDeg` | Number | 独立绝对编码器的最近反馈角度，单位 °；用于与 `steeringActualDeg` 交叉校验，必须结合 `encoderOnline` 和 `feedbackFresh` 使用。 |
| `steeringOnline` | Boolean | 转向节点当前是否在线。 |
| `driveOnline` | Boolean | 行走驱动节点当前是否在线。 |
| `encoderOnline` | Boolean | 独立绝对编码器反馈当前是否有效。 |
| `canBusOff` | Boolean | 底盘 CAN 是否检测到 Bus-Off；`true` 时禁止运动。 |
| `driveFault` | Boolean | 转向或行走驱动器是否报告故障；`true` 时禁止运动。 |
| `feedbackFresh` | Boolean | 本报告所用运动反馈是否仍在设备有效时限内。通信心跳不能将其刷新。 |
| `calibrated` | Boolean | 是否存在兼容的转向标定；`false` 时禁止普通运动。 |
| `powerReady` | Boolean | 是否满足设备接受运动目标所需的电源条件；不代表接触器或整车供电已被外部反馈确认。 |
| `powerOutputOn` | Boolean | A-BOX 的 MCU 电源输出命令锁存状态；不是触点或负载供电反馈。 |
| `otaBusy` | Boolean | OTA 是否正处于禁止运动的阶段；`true` 时禁止运动。 |
| `safetyReady` | Boolean | 当前是否通过接受非零运动目标的全部安全门禁。`true` 不代表车辆已经运动或环境安全。 |
| `steeringProfileReady` | Boolean | 当前保存的转向安全配置是否满足本产品运行要求。 |
| `steeringRangeValid` | Boolean | 当前独立转角反馈是否处于本产品正式运行范围。 |
| `stopReason` | Integer | 当前或最近一次安全停止原因码，取值见“停止原因码”。 |
| `faultLatched` | Boolean | 是否存在需要明确复位处理的锁存故障；停止命令和会话续租不能清除锁存故障。 |
| `steeringBoundary` | Integer | 转向边界状态：`-1` 负向边界、`0` 无边界限制、`1` 正向边界。非零时行走目标置零，只允许转向回退。 |

闲置周期状态每 120 秒使用 QoS 0 发布；控制、标定或短时诊断期间每 1 秒发布。状态变化、故障产生/解除和安全停止不等待周期，使用 QoS 1。平台订阅该 Topic 时请求 QoS 1，按 `reportId` 去重，并阻止较旧 `timestamp` 覆盖较新状态。低频期间保存的状态是历史快照，不可直接作为新的运动许可依据。

## 7. 运动安全语义

- `safetyReady=false` 时，设备不得接受非零运动目标。
- 改变前进/后退方向时，设备先发送零速；连续三次收到左右实测转速绝对值均不超过 `20 rpm` 后才应用反向目标。2 秒未确认静止时撤销会话并锁存故障。
- 控制租约超时、连接中断或安全条件失效时，设备必须进入安全停止状态。
- `code=200` 只表示请求已被接受；实际运动结果以状态报告中的实测字段为准。

## 8. 接入约束

- 接入方只按 `deviceType=vehicle_chassis` 选择本业务协议。
- 控制发布、控制响应和状态订阅必须使用本文规定的 QoS；请求重试沿用原 `requestId`。
- 控制响应按数值 `code` 判断是否接受，状态变化以主动状态报告为准。
- 平台上下电必须使用本 Profile 的公共 `/ctrl` 命令；运维接口不属于本协议。
- 心跳在线、`safetyReady` 和 `motionState` 表示不同层级的状态，不得互相替代。
