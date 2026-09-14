# A-BOX 车辆底盘业务协议 V3

协议版本：3.0

文档修订：1.2

设备类型：`vehicle_chassis`

## 1. 协议范围

本文只定义平台与车辆底盘 A-BOX 之间稳定、可观察的公共业务接口。它适用于具备纵向驱动、转向和独立运动安全仲裁能力的车辆底盘，不以客户、车型或项目名称区分协议。

通用报文外层、UTC 毫秒时间戳、QoS、请求幂等、公共响应码和心跳规则遵循《A-BOX 设备公共 MQTT 协议 V3》。设备运维接口、内部总线、硬件映射和控制实现不属于本文范围。

公共 V3 报文统一使用 `data.deviceType=vehicle_chassis`，不得携带内部产品标识。

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

心跳不得加入 `product`、VID、ICCID、Boot 版本、OTA 状态、CAN 状态或底盘反馈。心跳只证明 A-BOX 与 Broker 的通信存活，不证明下游节点、执行器、电源、机械动作或 OTA 正常。

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

| 字段 | 类型 | 必填 | 约束 |
|---|---|---:|---|
| `data.deviceType` | String | 是 | 固定为 `vehicle_chassis` |
| `data.requestId` | String | 是 | 平台生成的全局唯一请求标识；重试沿用原值 |
| `data.command` | String | 是 | 固定为 `chassis_control` |
| `data.params.action` | String | 是 | `acquire`、`renew` 或 `stop` |
| `data.params.sessionId` | String | 是 | 当前控制会话标识 |
| `data.params.seq` | Integer | 是 | 会话内严格递增，范围 `1..4294967295` |
| `data.params.leaseMs` | Integer | 是 | `1..1000` ms；`stop` 仍携带该字段 |
| `data.params.driveRpm` | Integer | 是 | `-200..200` rpm；正负方向由产品标定定义 |
| `data.params.steeringDeg` | Number | 是 | `-30.0..30.0`°；正负方向由产品标定定义 |

`acquire` 建立控制租约，`renew` 更新同一会话目标并续租，`stop` 明确请求零速和释放会话。平台停止发送、浏览器失联或租约超时后，设备必须安全停止，不得保持最后一次非零目标。

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

| 响应码 | 含义 |
|---:|---|
| `200` | 请求已被设备仲裁接受 |
| `400` | 请求合法，但当前运动安全条件不允许执行 |
| `4001` | JSON、通用字段、参数或序号非法 |
| `4002` | 不支持的命令或动作 |
| `4003` | 已被其他会话占用或控制流程繁忙 |
| `4004` | `deviceType` 不匹配 |
| `5001` | A-BOX 内部异常，未接受目标 |

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
    "faultLatched": false
  }
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `reportId` | String | 设备内唯一报告标识，平台据此去重 |
| `reportReason` | String | `periodic`、`state_changed`、`fault_raised`、`fault_cleared` 或 `safety_stop` |
| `motionState` | String | `stopped`、`driving`、`reversing_wait` 或 `fault` |
| `targetDriveRpm` | Integer | 当前仲裁后的驱动目标，rpm |
| `targetSteeringDeg` | Number | 当前仲裁后的转向目标，° |
| `leftActualRpm` / `rightActualRpm` | Integer | 左右驱动实测转速，rpm |
| `steeringActualDeg` | Number | 转向驱动器反馈角度，° |
| `absoluteActualDeg` | Number | 独立绝对编码器角度，° |
| `encoderOnline` | Boolean | 独立绝对编码器反馈当前是否有效 |
| `canBusOff` | Boolean | 底盘 CAN1 是否检测到 Bus-Off |
| `driveFault` | Boolean | 转向或双驱控制器是否报告驱动故障 |
| `feedbackFresh` | Boolean | 本报告使用的运动反馈是否在有效时限内 |
| `powerOutputOn` | Boolean | A-BOX 的 MCU 电源输出命令锁存状态；不是触点或负载供电反馈 |
| `safetyReady` | Boolean | 当前是否满足接受运动目标的全部门禁 |
| `steeringProfileReady` | Boolean | 当前保存的转向安全配置是否满足本产品运行要求 |
| `steeringRangeValid` | Boolean | 当前独立转角反馈是否处于本产品正式运行范围 |
| `stopReason` | Integer | 产品定义的最近安全停止原因码 |
| `faultLatched` | Boolean | 是否存在需要人工或明确命令处理的锁存故障 |

`stopReason=13` 表示执行流程故障锁存，`14` 表示编码器范围异常，`15` 表示编码器安全配置不匹配。停止命令和会话续租不能清除上述锁存。

闲置周期状态每 120 秒使用 QoS 0 发布；控制、标定或短时诊断期间每 1 秒发布。状态变化、故障产生/解除和安全停止不等待周期，使用 QoS 1。平台订阅该 Topic 时请求 QoS 1，按 `reportId` 去重，并阻止较旧 `timestamp` 覆盖较新状态。低频期间保存的状态是历史快照，不可直接作为新的运动许可依据。

## 7. 运动安全语义

- `safetyReady=false` 时，设备不得接受非零运动目标。
- 改变前进/后退方向时，设备必须先安全停止，再应用反向目标。
- 控制租约超时、连接中断或安全条件失效时，设备必须进入安全停止状态。
- `code=200` 只表示请求已被接受；实际运动结果以状态报告中的实测字段为准。

## 8. 接入约束

- 接入方只按 `deviceType=vehicle_chassis` 选择本业务协议。
- 控制发布、控制响应和状态订阅必须使用本文规定的 QoS；请求重试沿用原 `requestId`。
- 控制响应按数值 `code` 判断是否接受，状态变化以主动状态报告为准。
- 平台上下电必须使用本 Profile 的公共 `/ctrl` 命令，不得调用 `/internal/*` 运维 Topic。
- 心跳在线、`safetyReady` 和 `motionState` 表示不同层级的状态，不得互相替代。


### 最近故障快照

状态 data 可包含整数 `steeringBoundary`：0 表示无边界回退状态，-1 表示负向边界，1 表示正向边界。非零时行走暂停，向外转向请求受限，向内转向或回中请求仍须通过其他安全门禁；该状态本身不表示锁存故障。

状态 data 可包含 `faultRecordVersion: 1` 和 `lastFault`。后者为 29 个整数的有序数组；它代表最近一次故障，不代表当前仍处于故障。未采到样本的年龄为 4294967295，非零年龄的数据不得当作故障后读取。历史快照可在后续状态重复出现，以 VID、序号识别，不能据此重复执行控制。

| 下标 | 含义 |
|---|---|
| 0–4 | 序号、发生时启动毫秒数、来源（1 运动保护，2 中位标定）、来源原因码、门禁位图 |
| 5–10 | 目标转速、目标转角毫度、A/B 实际转速、驱动器原始位置、独立编码器原始位置 |
| 11–18 | 转向/行走状态字、A/B 故障码、A/B 故障码样本年龄、转向/行走状态字样本年龄（毫秒） |
| 19–24 | 转向阶段/步骤/执行错误、行走阶段/步骤/执行错误 |
| 25–28 | 最近 SDO Abort、编码器通信错误、中位会话阶段、保存状态（0 待保存，1 已保存，2 保存失败或空间不足） |

执行错误与厂商故障码属于不同命名空间。故障快照存在时可省略可选 `transportDiagnostics`。收到上报不作为设备执行停机的前置条件。
