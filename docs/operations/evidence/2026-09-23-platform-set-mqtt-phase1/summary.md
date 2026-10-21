# ABox SET_MQTT_CONFIG 首次现场联调（2026-09-23）

## 对象与边界

- 本机 J-Link SN 69730334 所接底盘；产品 `patrol_vehicle_chassis`，VID `PATROL-TLS-LAB-001`，固件 `patrol-chassis-0.2.33`。物理 App readback SHA256：`EF5DEC2765790CF18D2DFC27B9A89377F4748A87AE4D65FA1A1D285568BFEC5C`，与正式包一致。
- 平台部署 `0.4.134`；`PATROL_MQTT_CONFIG_FIRMWARES=patrol-chassis-0.2.33`。仅底盘当前固件可见 `set_mqtt_config` capability。TLS 目标为同一业务平台 `zxwl.ntiov.com:19688`、profile 0；plain 为 `zxwl.ntiov.com:19683`。
- 本次通过平台现有 operation 队列提交，以 `phase1-field-test` 为审计 actor。Worker、设备 MQTT 通道、operation、audit 和 protocol trace 均实际运行。由于未取得管理员 Web 会话，本次未从浏览器提交 API；API 权限及页面交互不是本证据验证范围。
- 未修改底盘固件、VID、ClientId、Topic 或其他产品。设备侧 Trial 的连接、订阅、证明和恢复由设备执行。

## 现场结果

| 场景 | operation ID | 设备响应及平台状态 | 设备/物理确认 |
| --- | --- | --- | --- |
| 基线 plain | `4538b076-8ec9-4bbd-854a-96b166a02a70` | `get_info` 200 succeeded | `36.153.162.171:19683`，TLS false，VID 不变，MQTT READY |
| plain→TLS | `ea8e018c-4c48-442c-a33f-759e293d79e2` | queued→sent→verifying（202 `MQTT_CANDIDATE_TESTING`）→succeeded（200 `MQTT_CONFIG_SAVED`） | 后续 `get_info`：`zxwl.ntiov.com:19688`、TLS true、profile 0、READY；Config V5 readback 相同 |
| TLS 配置重启 | `331c2c86-d84a-41db-9d50-84c4a038b92b` | J-Link 复位后 `get_info` 200 succeeded | uptime 64.1 秒；TLS 19688、READY、VID 不变；复位前后 Config SHA256 相同 |
| TLS→plain | `ed96d326-b661-4963-b201-a4afd6947d95` | queued→sent→verifying（202）→succeeded（200） | 后续 `get_info`：`zxwl.ntiov.com:19683`、TLS false、READY；Config V5 readback 一致 |
| 无效 TLS 候选恢复 | `37cac69a-10f8-479c-9c94-79df6612308c` | queued→sent→verifying（202）→failed（500 `MQTT_CANDIDATE_RESTORED`） | 暂时使用 `phase1-invalid.invalid` 作为候选；设备恢复 plain，Config SHA256 与失败前完全相同；最终 `get_info` 200、READY |

复位后的首次 `get_info`（`9cc4a7cc-75c3-41a0-a615-d6f7aa73587f`）在设备启动重连期间 30 秒超时；设备重新心跳后重试成功。不能把首次超时算作重启恢复成功证据。

## 物理 Config readback

| 阶段 | SHA256 | 解析字段 |
| --- | --- | --- |
| 切换前 | `A1E93C70B13B43FBF149402EE03FA8AC5A1B1FCF6A8004F2301C210DDD978835` | Config V5，VID `PATROL-TLS-LAB-001`，plain `36.153.162.171:19683` |
| TLS commit 后 | `4BC6CDCADBD327AB49175FEB6F254BAA6E93B84E5BC7A223BF07996F0FF328C7` | Config V5，TLS `zxwl.ntiov.com:19688`，profile 0 |
| TLS 复位后 | `4BC6CDCADBD327AB49175FEB6F254BAA6E93B84E5BC7A223BF07996F0FF328C7` | 与复位前逐字节一致 |
| plain commit 后 | `53B182CCD0383412013046C062C02B735C951054986BB535ED02B186A9D11B3D` | Config V5，plain `zxwl.ntiov.com:19683` |
| 失败恢复后 | `53B182CCD0383412013046C062C02B735C951054986BB535ED02B186A9D11B3D` | 与失败前逐字节一致 |

Config 原始页包含凭据，保留在本地 `out/logs/phase1-*-config.bin`，不复制进仓库证据目录。此处记录哈希及非敏感解析字段。

## 平台证据与发现

- `platform-operations-traces-audit.json` 保存上述 operation 状态事件、同 requestId 的 request/202/200 或 500 协议轨迹，以及三个切换 operation 的 `device_operation_completed` 审计。文件 SHA256：`99D12FBECC9CEFC19CB10CA21E2D3A3C99996A61E6E282B77433671BE42BF255`。不含 MQTT payload 或凭据。
- 协议轨迹的 request 与两个 response 可通过 `correlation_id`、`matched_request_id` 关联；但通用 trace 逻辑把同 requestId 的最终 200/500 标成 `duplicate_response`。这是轨迹分类缺口，不能据此认定设备重复发送；operation 状态与原始响应码无歧义。本轮未扩大功能范围修改该逻辑。
- 测试后将 TLS 目标恢复为 `zxwl.ntiov.com:19688`，profile 0；API 和 MQTT worker 均 `active`，`healthz` 为 `ok`。设备最终停留在 plain 19683、MQTT READY、VID 不变。
