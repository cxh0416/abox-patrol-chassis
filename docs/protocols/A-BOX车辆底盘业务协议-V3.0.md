# A-BOX 车辆底盘业务协议 V3

协议版本：3.0

文档修订：1.0

契约状态：已按本文完成固件与 OTA 平台源码切换及自动回归；尚未发布到生产平台，也未完成台架/实车联调

设备类型：`vehicle_chassis`

对应产品实现：`patrol_vehicle_chassis`

## 1. 协议范围

本文只定义平台与车辆底盘 A-BOX 之间稳定、可观察的公共业务接口。它适用于具备纵向驱动、转向和独立运动安全仲裁能力的车辆底盘，不以客户、车型或项目名称区分协议。

通用报文外层、UTC 毫秒时间戳、QoS、请求幂等、公共响应码和心跳规则遵循平台仓库的《A-BOX 设备公共 MQTT 协议 V3》。设备信息、运行日志、配置和 Boot V2 OTA 见 [巡防车底盘 A-BOX 内部运维协议](巡防车底盘A-BOX内部运维协议-1.0.md)。厂商 CANopen 对象字典、节点号、编码器帧、硬件引脚和内部状态机不属于公共业务协议。

公共 V3 报文统一使用 `data.deviceType=vehicle_chassis`，不得携带 `data.product`。产品标识 `patrol_vehicle_chassis` 只用于内部运维、制品和部署归属。

三类 A-BOX 产品使用同一分层规则：

| 产品实现 | 公共 V3 `deviceType` | 内部 V2 `product` |
|---|---|---|
| 送餐车双锁 | `locker_compartment` | `meal_delivery_vehicle` |
| 顶飞翼 | `wing_actuator` | `top_flying_wing` |
| 车辆底盘 | `vehicle_chassis` | `patrol_vehicle_chassis` |

`patrol-chassis-0.1.0` 使用旧的私有 Topic、旧响应字段和混合心跳，不视为本文协议的合规实现。源码默认版本 `patrol-chassis-0.1.1` 已切换本文契约；正式能力仍以发布产物、平台部署版本和台架/实车验收记录为准。

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

心跳每 5 秒发送一次，字段只包含公共协议规定的设备类型和当前 App 固件版本：

```json
{
  "version": "3.0",
  "timestamp": 1788134400000,
  "data": {
    "deviceType": "vehicle_chassis",
    "firmware": "patrol-chassis-0.1.1"
  }
}
```

心跳不得加入 `product`、VID、ICCID、Boot 版本、OTA 状态、CAN 状态或底盘反馈。心跳只证明 A-BOX 与 Broker 的通信存活，不证明下游节点、执行器、电源、机械动作或 OTA 正常。

示例中的 `patrol-chassis-0.1.1` 与当前源码默认版本一致；正式发布仍以制品中提取到的版本标记为准。

## 4. 底盘控制

### 4.1 请求

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
| `data.params.driveRpm` | Integer | 是 | `-300..300` rpm；正负方向由产品标定定义 |
| `data.params.steeringDeg` | Number | 是 | `-5.0..5.0`°；正负方向由产品标定定义 |

`acquire` 建立控制租约，`renew` 更新同一会话目标并续租，`stop` 明确请求零速和释放会话。平台停止发送、浏览器失联或租约超时后，设备必须安全停止，不得保持最后一次非零目标。

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

## 5. 主动状态

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
    "feedbackFresh": true,
    "calibrated": true,
    "powerReady": true,
    "otaBusy": false,
    "safetyReady": true,
    "stopReason": 0,
    "faultLatched": false
  }
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `reportId` | String | 设备内唯一报告标识，平台据此去重 |
| `reportReason` | String | `periodic`、`state_changed`、`fault_raised`、`fault_cleared` 或 `safety_stop` |
| `motionState` | String | `stopped`、`driving`、`reversing_wait` 或 `fault`；只能根据实测反馈和状态机给出 |
| `targetDriveRpm` | Integer | 当前仲裁后的驱动目标，rpm |
| `targetSteeringDeg` | Number | 当前仲裁后的转向目标，° |
| `leftActualRpm` / `rightActualRpm` | Integer | 左右驱动实测转速，rpm |
| `steeringActualDeg` | Number | 转向驱动器反馈角度，° |
| `absoluteActualDeg` | Number | 独立绝对编码器角度，° |
| `feedbackFresh` | Boolean | 本报告使用的运动反馈是否在有效时限内 |
| `safetyReady` | Boolean | 当前是否满足接受运动目标的全部门禁 |
| `stopReason` | Integer | 产品定义的最近安全停止原因码 |
| `faultLatched` | Boolean | 是否存在需要人工或明确命令处理的锁存故障 |

周期状态建议每 1 秒使用 QoS 0 发布；状态变化、故障产生/解除和安全停止使用 QoS 1。平台订阅该 Topic 时请求 QoS 1，按 `reportId` 去重，并阻止较旧 `timestamp` 覆盖较新状态。

## 6. 运动安全语义

- 未标定、反馈过期、双转角偏差超过 2°、任一必需节点离线、CAN Bus-Off、驱动故障、OTA 忙或电源未就绪时，`safetyReady=false`，不得接受非零运动目标。
- 改变前进/后退方向前，设备必须先明确发送零速；只有连续三次分别收到左右实测转速绝对值均不超过 20 rpm 的新反馈，才允许应用反向目标。
- 方向切换等待超过 2 秒时，设备必须撤销使能并锁存故障；后续租约续期不得自动解除该故障。
- 未确认继电器、接触器反馈和本地按键引脚前，硬件输出保持禁用。
- `code=200`、MQTT PUBACK、CAN 发送成功和目标值变化都不是实车动作完成证据。

## 7. 平台最小验收

- 只按 `vehicle_chassis` 选择本业务 Profile，不以 `product` 解析公共 V3 消息。
- 只实现本文声明的 4 个公共 Topic；设备信息、日志、配置和 OTA 只走 `/internal/*`。
- 控制发布与控制响应、状态订阅使用正确 QoS；请求重试沿用 `requestId`。
- 控制响应按 `code` 判断是否被接受，不读取 `commandAccepted` 或 `result`。
- 区分 A-BOX 心跳在线、CAN 节点在线、`safetyReady` 和实车 `motionState`。
- 不根据目标值或请求成功推断车辆已经运动、停止或到达角度。

## 8. 从 0.1.0 旧稿迁移

| 0.1.0 旧稿 | 本协议 | 处理要求 |
|---|---|---|
| `deviceType=patrol_vehicle_chassis` | `deviceType=vehicle_chassis` | 固件、平台发布端和订阅适配器同时切换，不做双值长期兼容 |
| `/chassis/control` | `/ctrl` | 统一到公共控制 Topic |
| `/chassis/control/resp` | `/ctrl/resp` | 统一到公共响应 Topic |
| `/chassis/status` | `/status/report` | 改为带 `reportId` 的主动状态报告 |
| `commandAccepted` | 数值 `code` + 字符串 `msg` | 平台只按 `code` 判断接受结果 |
| 心跳混入 Boot、OTA、CAN 和底盘状态 | 心跳仅含 `deviceType`、`firmware` | 运维字段迁到 `get_info`，业务状态迁到 `/status/report` |

迁移必须以同一发布窗口同时更新固件和 OTA 平台配置。旧固件继续按旧契约识别，但不得标记为 `vehicle_chassis` V3；平台不得在同一个设备记录上混合解析新旧 Topic。
