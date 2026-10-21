# A-BOX 巡防车底盘业务协议

文档修订：V4.0

公共报文版本：`4.0`

Profile：`vehicle_chassis 1.0`

## 1. 协议范围

本文定义巡防车底盘在 A-BOX MQTT V4 下的业务 Profile，包括车辆上下电、租约式底盘控制、完整底盘状态和业务安全语义。

公共报文外层、Topic、QoS、请求响应、公共响应码、`requestId` 幂等、Manifest、心跳和 `sync_state` 通用规则统一遵循 [A-BOX 设备公共 MQTT 协议 V4](https://github.com/cxh0416/abox-platform-firmware/blob/main/docs/protocols/A-BOX设备公共MQTT协议-V4.0.md)，本文不重复定义。

`/internal/*` 运维、标定及硬件映射、内部算法、操作和验收记录不属于本 Profile。

## 2. Profile 与能力

Profile 名称固定为 `vehicle_chassis`，不从客户或项目名称派生。本 Profile 为单实例能力，不使用 `instanceId`。

## 3. 能力声明与命令清单

Manifest Profile 条目固定为：

```json
{"name":"vehicle_chassis","version":"1.0"}
```

| 接口 / 匹配条件 | 分类 | params | expiresAt / 最大有效期 |
|---|---|---|---|
| `power_on` | DISCRETE_ACTION | null | 必填 / 5 秒 |
| `power_off` | SAFETY_STOP | null | 可选 |
| `chassis_control`, action=acquire/renew | CONTINUOUS_SESSION | 第 4.3 节 | 不使用 |
| `chassis_control`, action=stop | SAFETY_STOP | 第 4.3 节 | 不使用 |
| `chassis_status` | state，完整状态 | 第 5 节 payload | — |

同一固件固定提供该能力时可以省略 `manifestId`；其他 Manifest 规则遵循公共 V4。

## 4. 业务命令定义

有效期、可信时间、幂等和异常响应遵循公共 V4 第 4 节；本 Profile 的有效期限制见第 3 节。`sync_state` 和连续控制不使用 `expiresAt`。

### 4.1 `power_on`

- `code=200` 表示 MCU 电源输出命令已锁存；`result.powerOutputOn`（Boolean）表示当前命令锁存状态；
- 成功不表示负载端或整车已经真实上电；运动目标仍由 MCU 实时安全条件决定。

### 4.2 `power_off`

- `code=200` 表示 MCU 电源输出命令已关闭，当前运动目标和控制会话已撤销；
- `result.powerOutputOn`（Boolean）表示当前命令锁存状态；成功不表示真实断电或车轮已经静止；
- 本操作为安全停止，允许重复执行。

电源开启请求及成功响应示例（只表示命令锁存，不表示真实供电反馈）：

```json
{
  "version": "4.0",
  "timestamp": 1800000000000,
  "data": {
    "requestId": "patrol-power-001",
    "command": "power_on",
    "params": null,
    "expiresAt": 1800000003000
  }
}
```

```json
{
  "version": "4.0",
  "timestamp": 1800000000100,
  "data": {
    "requestId": "patrol-power-001",
    "code": 200,
    "msg": "operation accepted",
    "result": {
      "powerOutputOn": true
    }
  }
}
```

### 4.3 `chassis_control`

#### 4.3.1 `params` 字段

| 字段 | 类型 | 必填 | 单位/范围 | 含义 |
|---|---|---:|---|---|
| `action` | String | 是 | `acquire` / `renew` / `stop` | 建立租约、更新目标并续租，或请求零速并释放会话。 |
| `sessionId` | String | 是 | 1～39 个字符 | 控制会话标识；不得用 `requestId` 替代。 |
| `seq` | Integer | 是 | `1..4294967295` | 会话内序号；`acquire/renew` 必须严格递增；`stop` 不依赖序号递增。 |
| `leaseMs` | Integer | 是 | ms；`1..1000` | 远控目标最长保持时间；到期未续租时撤销远控目标并停止行走。`stop` 也须携带。 |
| `driveRpm` | Integer | 是 | rpm；`-1000..1000` | 行走目标；正负方向由产品标定定义，`0` 为零速。 |
| `steeringDeg` | Number | 是 | °；`-30.000..30.000`，精度 `0.001°` | 轮胎转向角目标，`0` 为回中。 |

非零 `driveRpm` 只有在兼容标定、有效控制会话及 MCU 当前运行安全条件满足时才接受；MCU 对目标是否继续执行作实时安全裁决。无需额外行走许可。

无有效远控目标时，设备可在本地安全条件满足、行走已确认停车后自行回中，包括上电后的空闲状态和远控租约到期。该行为不恢复远控会话、不允许非零行走目标；有效的新远控目标可接管转向。`stop` 和 `power_off` 保持安全停车语义，取消自动回中；新的有效控制目标或显式上电可重新允许空闲回中。目标为 `0` 不表示已经回中，仍以两路实测转角判断。

`stop` 先将本地目标归零并请求安全停车，不因有效格式的 `sessionId` 不匹配或 `seq` 未递增而保留非零目标。会话匹配时同时释放会话；不匹配时保留原会话归属，不让请求方抢占控制权。重复 `stop` 安全且幂等。`code=200` 仅表示本地停车请求已受理，不证明车轮已静止。

#### 4.3.2 成功结果

`code=200` 只表示请求通过会话和安全仲裁并更新设备目标，不表示车辆已运动、到位或停止。`data.result` 包含：

| 字段 | 类型 | 含义 |
|---|---|---|
| `targetDriveRpm` | Integer | 当前仲裁的行走目标，单位 rpm；`0` 不表示车轮已静止。 |
| `targetSteeringDeg` | Number | 当前仲裁的转向目标，单位 °；不是实测转角。 |
| `stopReason` | Integer | 当前或最近一次安全停止原因，见第 7.2 节。 |

## 5. 状态报告定义

### 5.1 `chassis_status`

`chassis_status` 是完整状态快照，不是增量补丁。公共 Report 字段遵循公共 V4；业务 `payload` 定义如下：

| 字段 | 类型 | 含义 |
|---|---|---|
| `reportReason` | String | `periodic`、`reconnect`、`state_changed`、`fault_raised`、`fault_cleared` 或 `safety_stop`。 |
| `motionState` | String | `fault`、`reversing_wait`、`driving` 或 `stopped`。反馈过期时 `stopped` 不证明车轮静止。 |
| `targetDriveRpm` | Integer | 当前行走目标，单位 rpm；不是实测值。 |
| `targetSteeringDeg` | Number | 当前转向目标，单位 °；不是实测值。 |
| `leftActualRpm` / `rightActualRpm` | Integer | 左右驱动最近实测转速，单位 rpm，须结合 `feedbackFresh`。 |
| `steeringActualDeg` | Number | 转向驱动器最近反馈角度，单位 °，须结合 `feedbackFresh`。 |
| `absoluteActualDeg` | Number | 独立编码器最近反馈角度，单位 °，须结合 `encoderOnline` 和 `feedbackFresh`。 |
| `steeringOnline` / `driveOnline` / `encoderOnline` | Boolean | 对应节点或反馈是否在线有效。 |
| `canBusOff` | Boolean | CAN 是否 Bus-Off；为 `true` 时禁止运动。 |
| `driveFault` | Boolean | 驱动器是否报告故障；为 `true` 时禁止运动。 |
| `feedbackFresh` | Boolean | 运动反馈是否仍在有效时限内；心跳不能刷新该状态。 |
| `calibrated` | Boolean | 是否具备兼容的转向标定。 |
| `steeringProfileReady` | Boolean | 转向安全配置是否满足运行要求。 |
| `steeringRangeValid` | Boolean | 独立转角反馈是否位于允许运行范围。 |
| `powerReady` | Boolean | 是否满足接受运动目标的电源条件；不是外部供电反馈。 |
| `powerOutputOn` | Boolean | MCU 电源输出命令锁存状态；不是触点或负载反馈。 |
| `otaBusy` | Boolean | OTA 是否处于禁止运动阶段。 |
| `safetyReady` | Boolean | 设备当前计算的运动就绪观测值；不表示车辆已运动或环境安全。 |
| `stopReason` | Integer | 安全停止原因，见第 7.2 节。 |
| `faultLatched` | Boolean | 是否存在必须明确复位的锁存故障。 |
| `steeringBoundary` | Integer | `-1` 负向、`0` 无、`1` 正向边界；非零时禁止行走，只允许转向回退。 |
| `faultRecordVersion` | Integer | 故障记录结构版本，当前为 `1`。 |
| `lastFault` | Array<Integer> | 可选的版本化历史故障记录。 |
| `transportDiagnostics` | Object | 可选通信诊断计数，仅用于观测，不参与运动许可。 |

`lastFault` 与 `transportDiagnostics` 至多出现一个。目标值、节点在线或公共心跳均不能替代新鲜的实测反馈。

`safetyReady` 及其组成字段用于状态展示、诊断和操作提示，不构成平台侧 `acquire/renew` 的额外授权门禁。平台可提示设备未就绪，但不得复制 MCU 的安全条件组合来决定是否发送控制请求；设备对请求的实时响应及持续安全裁决为准。

## 6. `sync_state` 状态集合

`sync_state` 的通用规则由公共 V4 定义。本 Profile 的预期完整状态集合为：

```json
{"expectedStates":[{"reportName":"chassis_status"}]}
```

设备随后发布完整 `chassis_status`，并按公共规则关联同一 `requestId`。本 Profile 为单实例，因此不携带 `instanceId`。

## 7. 底盘专用错误及安全语义

### 7.1 业务失败语义

公共错误码遵循公共 V4。电源输出不可用或写入失败时不得报告成功；目标超出范围、安全条件不满足、`acquire/renew` 会话不匹配或流程繁忙时不得更新为非零目标或抢占有效会话；内部异常不得通过成功响应掩盖。

### 7.2 `stopReason`

| 值 | 名称 | 含义 |
|---:|---|---|
| `0` | `none` | 无停止原因。 |
| `1` | `reserved` | 保留值，正式运行不产生；后续编号保持不变。 |
| `2` | `uncalibrated` | 无可用转向标定。 |
| `3` | `feedback_stale` | 运动反馈过期。 |
| `4` | `angle_mismatch` | 双转角反馈偏差超限。 |
| `5` | `node_offline` | 必需节点离线。 |
| `6` | `can_bus_off` | CAN Bus-Off。 |
| `7` | `drive_fault` | 驱动器故障。 |
| `8` | `ota_busy` | OTA 忙。 |
| `9` | `power_not_ready` | 电源条件不满足。 |
| `10` | `lease_expired` | 控制租约到期。 |
| `11` | `direction_timeout` | 换向静止确认超时并锁存故障。 |
| `12` | `explicit_stop` | 明确停止。 |
| `13` | `execution_fault` | 执行故障已锁存。 |
| `14` | `encoder_range` | 独立转角超出运行范围。 |
| `15` | `encoder_profile` | 转向安全配置不满足要求。 |

### 7.3 安全语义

- MCU 当前安全条件不满足时拒绝控制目标，并在持续执行中请求安全停止；
- 未标定、反馈过期、双转角偏差超限、节点离线、CAN Bus-Off、驱动故障、OTA 忙或转向配置无效时禁止运动；
- 改变前进/后退方向前先发送零速，并连续三次确认左右实测转速绝对值不超过 `20 rpm`；2 秒超时撤销会话并锁存故障；
- 租约超时、连接中断或安全条件失效时进入安全停止；
- `stop`、`power_off` 或成功响应均不能单独证明机械停止，必须结合新鲜实测反馈判断。
