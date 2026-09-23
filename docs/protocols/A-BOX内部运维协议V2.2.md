# A-BOX 内部运维协议 V2.2

## 1. 适用范围

本文定义巡防车底盘 A-BOX 当前实现的设备信息、VID/MQTT 配置、远程日志、只读诊断、标定维护、受控重启和 Boot V2 OTA 接口。

公共业务遵循平台仓库的 [A-BOX 设备公共 MQTT 协议 V4](https://github.com/cxh0416/abox-platform-firmware/blob/master/docs/protocols/A-BOX设备公共MQTT协议-V4.0.md)；底盘业务遵循 [vehicle_chassis 1.0 Profile](A-BOX巡防车底盘业务协议-V4.0.md)。本文的 `/internal/*` Topic 不是公共业务接口，不定义或改变底盘公共控制、安全仲裁及状态语义。

内部运维报文使用 `version: "2.0"`、UTC 毫秒 `timestamp` 和 `data` 对象。产品标识固定为 `patrol_vehicle_chassis`；请求中的 `data.product` 可省略，携带时必须匹配，响应和日志必须携带该值。

## 2. 内部 Topic

`{deviceId}` 与设备当前 VID 相同。

| 方向 | 用途 | Topic | QoS | Retain |
|---|---|---|---:|---:|
| 平台 → A-BOX | 配置、查询和诊断 | `/zxwl/abox/{deviceId}/internal/config` | 1 | 否 |
| A-BOX → 平台 | 配置、查询和诊断响应 | `/zxwl/abox/{deviceId}/internal/config/resp` | 1 | 否 |
| 平台 → A-BOX | Boot V2 OTA 请求 | `/zxwl/abox/{deviceId}/internal/ota` | 1 | 否 |
| A-BOX → 平台 | Boot V2 OTA 响应 | `/zxwl/abox/{deviceId}/internal/ota/resp` | 1 | 否 |
| A-BOX → 平台 | 日志分片和主动告警 | `/zxwl/abox/{deviceId}/internal/log` | 主动日志 0；查询分片 1 | 否 |

A-BOX 只订阅自身 VID 的精确配置和 OTA Topic。内部响应优先于日志发布。

## 3. 请求与响应

内部请求完整 UTF-8 JSON 最大 1023 字节。设备生成的单条内部响应或日志 JSON 必须小于 1080 字节；超限内容不得以截断 JSON 发布，诊断和日志应使用既有摘要或分片机制。

配置、查询和诊断请求使用：

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

响应使用：

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

平台必须按精确 Topic、`product` 和 `requestId` 关联响应。相同请求重试沿用原 `requestId`；设备仍在发送前一次响应、日志快照或等待重启时，可以返回或保持 `DEVICE_BUSY`。接收方应忽略 `result` 中未知的兼容扩展字段。

## 4. `internal_config` 能力

### 4.1 动作登记

| `params.action` | 能力 |
|---|---|
| `get_info` | 查询 App、Boot、OTA、MQTT、电源及 CAN 摘要 |
| `set_mqtt` | 试连并提交 VID、Broker、凭据和 MQTT TLS 策略 |
| `get_log` | 获取当前内存日志快照 |
| `reboot` | 在 Boot 状态允许时响应后受控重启 |
| `get_can_diag` | 查询 CAN、节点、反馈、编码器和 SDO 只读诊断 |
| `get_calibration` | 查询转角标定、产品配置兼容性和远程使能状态 |
| `set_calibration` | 在全部维护门禁满足时保存逐车标定，保存后保持远程运动禁用 |
| `clear_calibration` | 清除标定并关闭远程运动使能 |
| `start_center_calibration` | 创建受限中位标定会话 |
| `get_center_calibration` | 查询并续租中位标定会话 |
| `jog_center_calibration` | 在会话内执行一次受限转向点动 |
| `commit_center_calibration` | 异步写入编码器中位并保存逐车中位 |
| `cancel_center_calibration` | 停止点动、撤销许可并结束会话 |
| `arm_bench_steering` | 创建仅转向台架许可 |
| `arm_bench_drive` | 创建限速行走与转向台架许可 |
| `get_bench_steering` | 查询台架许可、阶段、故障和限制 |
| `stop_bench_steering` | 撤销台架许可并请求停止 |
| `reset_motion_fault` | 在车辆电源关闭且 OTA 空闲时复位运动故障 |
| `power_on` / `power_off` | 旧平台迁移兼容；当前平台不得使用 |

内部兼容上下电动作不属于当前公共接口。当前平台必须使用 `vehicle_chassis 1.0` 的 `power_on` / `power_off`。

### 4.2 `get_info`

`get_info.result` 至少提供以下能力组：

| 分组 | 字段 |
|---|---|
| 固件与 Boot | `fwVersion`、`bootVersion`、`bootDescriptorValid`、`bootApiVersion`、`bootFeatures` |
| 设备与 MQTT | `vid`、`uptimeMs`、`mqttState`、`mqttStateCode`、`mqttHost`、`mqttPort`、`mqttTls`、`mqttTlsProfileId`、`iccid` |
| OTA | `otaScheme`、`otaDownloader`、`otaState`、`otaPhase`、`otaReady`、`otaBusy`、`otaTargetVersion`、`stableSlot`、`candidateSlot`、`trialFailCount`、`lastOtaError` |
| 电源 | `powerOutputAvailable`、`powerOutputOn` |
| CAN 摘要 | `canInitialized`、`canBusOff`、`canRxOverflow`、`diagnostics` |

`diagnostics` 包含 CAN 收帧数、节点心跳、原始转向位置、左右轮速、编码器值和在线状态，以及 SDO/编码器超时计数。这些字段只用于诊断，不能证明标定、真实运动、真实供电或现场验收成功。

OTA 平台只能用最新 `get_info` 的实测 Boot 描述符、`otaReady`、`otaState` 和实际 `fwVersion` 判断升级条件及结果。

### 4.3 `set_mqtt`

参数如下：

| 字段 | 类型 | 含义 |
|---|---|---|
| `vid` | String | 新设备 VID，同时成为 MQTT ClientId 和 Topic 中的 `{deviceId}` |
| `host` | String | MQTT Broker 主机名或地址 |
| `port` | Integer | MQTT 端口 |
| `username` | String | 设备用户名 |
| `password` | String | 设备密码 |
| `tlsEnabled` | Boolean | `false` 使用明文 MQTT；`true` 使用 MQTT TLS |
| `tlsProfileId` | Integer | TLS profile 标识，范围 0～255 |

以上字段共同组成一个不可拆分的 MQTT candidate。设备必须在旧连接上确认受理后，
依次执行 candidate 准备、连接、订阅、指定 proof、持久化回读和提交；任一步失败均
恢复完整 active 配置。明文与 TLS 的任意方向切换都使用同一 trial，不得先保存再重启
碰运气。

当 active 为 TLS 时，TLS 失败不得自动尝试明文。只有收到明确的
`tlsEnabled=false` candidate 且明文 trial 全部成功后，才允许保存并切回明文。
成功响应表示 candidate 已完成 proof、持久化回读并成为 active；它不代表现场网络、
Broker 或后续长连接永远可用。

### 4.4 日志

`get_log` 将内存日志快照发布到 `/internal/log`。查询分片携带 `product`、`requestId`、`sequence`、`total` 和 `lines`；主动告警携带 `product`、`level` 和 `lines`。

日志是诊断流量，不得用于创建设备、刷新在线状态、确认命令完成或判断产品能力。公共业务平台不得订阅或解释该 Topic。

### 4.5 诊断与维护

`get_can_diag` 是严格只读查询，不发送 NMT、SDO 下载、PDO 目标、驱动使能或电源命令。结果覆盖：

- CAN1/CAN2 原始收帧、业务收帧、忽略帧、溢出和 Bus-Off 当前/锁存状态；
- 转向与行走节点心跳、NMT 状态、状态字和反馈新鲜度；
- 左右实测转速、转向原始位置及独立编码器值、在线和配置状态；
- 编码器超时、CRC、异常响应、格式及传输错误；
- SDO 成功、超时、Abort、发送错误、对象值及成功/Abort 位图。

历史值必须结合在线和新鲜度字段解释，初始零值不代表真实反馈。

标定、台架许可、中位点动和运动故障复位属于受控维护能力。响应只表示请求被接受或当前阶段变化，不能替代后续状态、反馈和现场安全确认。具体操作门禁、步骤和验收要求见内部运维指南，不属于公共业务 Profile。

## 5. OTA

OTA 请求发送到 `/internal/ota`：

```json
{
  "version": "2.0",
  "timestamp": 1788134400000,
  "data": {
    "requestId": "uuid",
    "command": "ota_upgrade",
    "params": {
      "url": "https://ota.example/fw-revisions/patrol_vehicle_chassis/patrol-chassis-0.2.31/patrol_vehicle_chassis_app.bin",
      "version": "patrol-chassis-0.2.31",
      "size": 109872,
      "crc32": "1234ABCD"
    }
  }
}
```

约束：

- `version` 必须使用 `patrol-chassis-` 前缀并符合固件版本格式；
- `size` 为正整数且不超过产品 App 区 `0x36000` 字节；
- `crc32` 为恰好八位的十六进制字符串；
- `url` 必须通过产品 OTA origin、产品目录、版本和制品名校验；
- OTA 忙、Boot 状态不允许或下载器未就绪时不得开始新任务；
- 长度、CRC32、向量表和候选文件校验完成后才可进入待安装状态。

下载优先使用 EC800 HTTPS/UFS 文件方式；不支持或失败时可回退到受控 Range 分片。下载或校验失败不得破坏稳定镜像。

`code=200` 和 `INSTALL_PENDING` 只表示候选镜像已校验并提交安装，不表示安装或试运行成功。平台必须持续通过 `get_info` 判断：

| 状态 | 含义 |
|---|---|
| `INSTALL_PENDING` / `INSTALLING` | 候选待安装或正在安装 |
| `TRIAL` | 候选试运行中，尚未确认 |
| `CONFIRMED` | 候选已满足确认条件 |
| `ROLLBACK` | Boot 已恢复稳定镜像，本次任务失败 |

只有实际 `fwVersion` 等于目标版本且 `otaState=CONFIRMED`，才能判定 OTA 完成。

## 6. 边界与安全要求

- `/internal/*` 不承载公共底盘业务命令或 `chassis_status`；
- 诊断成功不表示真实运动、转角、供电或 OTA 已完成；
- `powerOutputOn` 仅是 MCU 输出命令状态，不是继电器或接触器反馈；
- 写配置、标定、台架许可、故障复位、重启和 OTA 必须服从设备当前门禁，不得因运维请求绕过安全仲裁；
- 内部运维结果、实现细节和现场记录不得写入《A-BOX巡防车底盘业务协议-V4.0》；
- 历史操作参数和验收记录不能直接作为新固件操作步骤。
