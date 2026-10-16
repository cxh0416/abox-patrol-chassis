# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。正式状态由 [dist 清单](../dist/release.manifest.json) 与实际固件哈希确定；源码版本不代表已发布。

同事接手所需仓库、环境、构建测试、运动许可排查和现场待办见[项目交接与接手指南](operations/项目交接与接手指南.md)。

2026-10-14 当前源码、正式包和本车现场固件为 `patrol-chassis-0.2.61`。回中后撤使能确认超时已按 MCU/CAN 时序修复，平台 `0.4.185` 已接入接管确认中的下电复位与新状态等待。修复后的接管、续租、结束会话、本地回中和再次接管均已实际验证，最后保持下电且 faultLatched=false。完整证据见[本次索引](operations/evidence/2026-10-14-can-stop-fix/index.json)。Config V5 与公共 MQTT V4 保持冻结；跨重启完整动作账本仍未接入。

2026-10-15 协议优化已落实到未提交源码，构建标识为 `patrol-chassis-0.2.62`：公共完整状态保留必填字段，省略可选大诊断，并分别约束 payload 768 字节和整包 1024 字节；VID 参数表、配置切换回执路由和丢回执对账边界已校正。真实编码器的长元数据及极值反馈夹具最大整包 853 字节，ARM Release 与服务栈检查通过。正式打包在干净工作区检查处拒绝，现有 dist 保留；0.2.62 尚未发布、刷写或做现场验证。最近一次正式包及现场验证仍按下列 0.2.61 证据记录解释。

## 当前正式包

2026-10-14 正式包为 `patrol-chassis-0.2.61`，App SHA256 `3990B87DC261692E429CD807327B8221CF81A238EA4ADC615E124645D63E67B6`，148,520 字节。源码提交 `586dd010c790cb488ddada5cc6462f4f906c0d6a`，公共平台固定 `bc64f52b290521f53ececa626dc91765861845f8`。正式构建经 VerifiedCandidate 与实测候选逐字节一致，再整体替换 dist；冻结 Boot 2.3.0、RAM 覆盖 61,432 字节及页边界不变。

PP 回中后的 Shutdown 写请求先应答，状态 6041 仍保持 Operation Enabled；旧 200 ms 窗口会误触发 step 22/fault 3。转向停机确认改为最多 500 ms，单条 SDO 100 ms 与确认后收尾 100 ms 继续独立有界，不重放运动写入。实板稳定读帧确认 140 ms 时仍为 0x2237，后续为 0x2231，执行器正常回到 IDLE，未触发锁存。41 项 Python 回归、产品 C 夹具、ARM/栈检查通过；恢复流程 26 项前端脚本和 15 项远控后端测试通过。

本车 J-Link App 与新启动元数据逐字节读回一致，配置页 SHA256 `58081e976d2b7c439dbca0f114b61916e1c6ae1ce4993f3130043ecb64fc0847` 保留，独立 get_info 确认 0.2.61、CONFIRMED、TLS READY、OTA 空闲就绪。修复后的双路转角反馈完成回中，随后再次接管成功并连续续租 40 次、约 8 秒，行走目标为 0 rpm，最终下电及 faultLatched=false 已读回。未用外部设备测量机械角度或测试行走。一次早期 get_info 和一次下电回执超时仍按未知记录，后续完整验证没有重写其原结果。下列 0.2.60 等为本日较早快照。

设备 `PATROL-05D3FF343733574E43043550` 已实际由 `0.2.58` 刷入 `0.2.60`：程序与启动状态逐字节读回匹配，运行后自动建立当前 UFS 稳定镜像，独立查询为 `CONFIRMED`、MQTT READY、OTA 空闲就绪，trialFailCount/lastOtaError 均为 0。随后仅更新 MQTT 字段和 PSK 扩展记录，VID、标定与其他配置字节保留；第 6 代 PSK 使用 profile 0、公网 19684，真实 TLS 1.2／`0xCCAC` 会话与 Broker 端点匹配，普通启动后的 Flash 实读一致，平台入网 `verified`。普通 0.2.60 缺少远程首次 PSK 迁移入口及 get_info PSK 字段，本次持久参数证明来自独立 SWD 读回，经原 worker/internal evidence 校验后确认；没有补充固件功能。

13:42:34 设备确认平台授时 RTT 239 ms、误差界 151 ms；13:44:44 审计上电操作返回 200，13:44:46 独立 get_info 确认输出开启且驱动、转向、编码器均在线。未读取继电器/接触器触点反馈，未下发运动。原始记录、受控备份路径和 SHA256 见[现场索引](operations/evidence/2026-10-14-jlink-0260/index.json)。随后运行未通过：13:44:49 自动回中接近零角后的转向停机/撤使能确认超时（bench step 22、fault 3），首次故障序号 5，锁存执行故障 stopReason 13 并关闭输出。13:45:34 ljk 与 13:48:28 admin 的接管会话均因设备拒绝结束；admin 设备响应为 400 / motion safety gate rejected。后续再次上电恢复节点和反馈，但不会清除锁存故障；当前不能据前面的瞬时上电读回宣称远控或持续上电验收通过。原始停机 CAN 时序尚缺，100/200 ms 窗口具体过期原因待定位；本次诊断未发动力、复位或运动命令。见[后续拒绝证据](operations/evidence/2026-10-14-takeover-rejection/index.json)。下面为此前失败快照。

2026-10-12 在线设备 `PATROL-05D3FF343733574E43043550` 的单车 `0.2.45 → 0.2.54` OTA 失败：13:04:35 CST 回报 `INSTALL_PENDING`，13:18:28 独立 `get_info` 读到旧版本 `0.2.45`、`ROLLBACK`、`trialFailCount=3`、`lastOtaError=5`；平台任务随即从 waiting_version 更新为 failed。13:22 通过审计 get_log 取回的 16 条记录全部属于回滚后的旧版启动，不能作为新固件试运行日志。现有日志在重启时清空，Boot 消费并清除故障邮箱，get_info 的 resetReason 来自 App 读取的已清除 RCC 标志，不能据其 0 值排除看门狗或软件复位。具体试运行失败原因尚未定位；现场没有主机，本次没有操作探针、重启、重试 OTA 或运动。完整证据及定位边界见 [本次原始记录索引](operations/evidence/2026-10-12-054-ota-rollback/index.json)。本地主机构建复现的 App 哈希与 dist 和服务器 revision 完全相同，BSS/故障邮箱边界及 TIM2 向量检查正常；这些静态检查不能替代故障现场记录。

## 公共平台收尾

内部 V2 包络与响应编码已同步公共组件，严格解析与历史错误映射保持，公共 V4 和 Config ABI 不变。公共平台 48 项 CTest、三产品固定基线响应逐字对照、本产品 40 项 Python 及 C 夹具通过。

正式编译继续启用转向、驱动和执行输出，startup_motion_permit=false。首次按完整正式选项链接时 BSS 超出固定故障邮箱边界 84 字节；移除 MQTT adapter 未使用的默认发布 Topic 副本及重复 transport 配置缓存，ARM 静态 RAM 回收 132 字节。桥接器复制临时配置描述符，字符串和订阅数组仍由产品长期持有；实际 AT 握手、复制后配置更新与重连夹具通过。协议尺寸、通信缓冲、日志容量和运动准入没有缩减，固定邮箱仍为 0x2000BC40；链接边界断言通过。新增初始化/配置调用链分别为已知帧 984/264 字节，各加 1,024 字节保留量不超过 4,096 字节，静态估计不等于现场栈高水位。

历史 TLS CA/主机名/有效期验证仍按原设备、版本和条件解释；当前正式包生成不能替代本产品完整网络、OTA、故障回滚或机械验收。

三产品固定相同公共平台提交，具体 SHA 以子模块与正式清单为准。构建接入 `abox_platform_attach_standard_app`；运行入口为 `ABoxAppService_Init/Poll`，通信只有一个 owner。公共 Cloud port 统一响应、启动、关联状态、事件、周期及精确回执；产品提供 Profile、报文、状态和业务 handler。维护使用 `ABoxMaintenanceService` 组合 trial/executor/ConfigStore。OTA 采用现有 Boot V2 App 状态机和公共 bridge 暂停/工作区接线。

产品 Config V5（400 字节） 字节 ABI 保持不变，公共 MQTT V4 冻结。真实轮速、反馈时效、转角、CAN 故障及 OTA 门禁不变；方向改变仍要求零速及连续三次左右实测不超过 20 rpm，2 秒超时撤销使能并锁存故障。正式编译选项沿用原正式版；构建不证明运动或机械反馈。

## 发布与资源

使用公共 `platform/tools/build_product_release.ps1 -ProductRoot <本仓> -AppVersion <版本>`。产品显式声明身份、版本参数与发布合同。只构建 App，消费冻结 Boot；暂存校验及产品门禁全部通过后整体替换 dist，失败保留或恢复原包。

Boot `0x08000000`、App `0x08008000`、State A/B `0x0803E800/0x0803F000`、配置页 `0x0803F800`。冻结 Boot 2.3.0 SHA256 `981409EF107D5F5C56A1A80FCE107C0292E2C1165668C0279BA6158F24A61C63`。App 上界以产品发布合同为准。固定故障邮箱、RAM/heap/stack 链接断言保持；发布前核对 `tools/service_stack_budget.json` 的 owner 调用链与编译器 `.su`，静态检查不等于运行时栈高水位。

当前正式构建 App Flash 148,520 字节、RAM 地址覆盖 61,432/65,536 字节。维护保存链已知帧加 1,024 字节保留量不超过 4,096 字节；此结果为静态估计。

## 验证与边界

本地空闲回中已接入产品源码：没有有效远控目标（包括租约到期）时，行走先确认停车并撤使能，安全条件满足后由本地转向执行器回中，不恢复行走授权；两路新鲜转角连续三组在 ±1° 内结束。新有效远控可接管，显式 stop / 下电及安全故障取消；总过程 30 秒超时进入执行故障。行为与流程见 [远控与本地回中](operations/架空行走与平台远控实现.md)。产品 40 项 Python 与 C 夹具通过；覆盖真实控制仲裁、租约撤销后回中、行走停车等待、接管、停车竞态、门禁、双角度新样本与超时。现场诊断原始材料见 [回中中断证据](operations/evidence/2026-09-30-remote-center-timeout/index.json)。包含新逻辑的 0.2.54 单车 OTA 试运行失败并回滚，尚未做真实转向、回中或整车机械验收；不能将主机测试视为现场通过。

公共最终 48 项 CTest 通过；本产品 40 项 Python 与产品 C 夹具通过。三产品正式 ARM Release 与声明的维护/授时调用链栈检查通过。公共逻辑仅运行一轮最终主机检查；产品只验证差异，修改或失败后只重跑受影响部分。产品 Profile、codec、证明、安全与幂等的既有证据继续有效。阶段记录和原始日志不改写，源码、主机、ARM、标准板及实车证据分别表述。

共享标准板 UID `05DAFF383734525243054363` 曾验证送餐车候选 1.1.39 空配置入网、独立 get_info 和 MQTT/OTA 就绪；这是前期候选证据，不能代替最终源码实板验证。COM3 是 485 业务接口，接收到了业务数据。当前没有可控整板电源；冷上电/pre-App 停机、完整蜂窝断网及真实持续 OTA 下载/恢复尚未实板验收。Broker kick 和就绪字段不能替代这些验证。没有下发 OTA，也没有进行现场车辆运动或机械验收。

此前已关闭阶段见 [历史记录](operations/history/2026-09-30-cloud-foundation-pre-close.txt)。原始 evidence 及既有哈希保持不变。

## 时间模型修复

源码版本 patrol-chassis-0.2.51 已接入公共 TIM2 monotonic、ClockSnapshot 与可信 UTC；CCLK只bootstrap，TLS上线/重连三次采样并按误差预算周期复校。有效期上限只校验 expiresAt-request.timestamp，已接受请求先重放，UTC及monotonic双保护阻止校时前跳提前淘汰。公共业务包络和Config ABI保持。时间敏感新请求没有可信平台授时时返回4003；安装前必须部署授时服务及精确ACL。

本产品只完成主机回归和ARM构建；共享送餐车标准板验证5次Flash保存403ms及32秒关中断不丢时，不能替代本产品实车验证。动作重放为RAM模型，跨重启持久账本仍需独立落实；本轮不宣称跨重启幂等验收。
