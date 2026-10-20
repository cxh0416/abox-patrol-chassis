# 当前项目状态

本文件是 `patrol_vehicle_chassis` 的唯一当前状态入口。正式状态由 [dist 清单](../dist/release.manifest.json) 与实际固件哈希确定；源码版本不代表已发布。

同事接手所需仓库、环境、构建测试、运动许可排查和现场待办见[项目交接与接手指南](operations/项目交接与接手指南.md)。

2026-10-20 当前正式包和第二台车现场 App 均为 `patrol-chassis-0.2.63`。用户明确当前硬件测试目标为电脑键盘经 CAN 分析仪接 A-BOX CAN2，暂不处理无线遥控器协议；网页只用于中位标定等维护，不作为本次遥控验收路径。外部原入口 `E:\1_work\ChassisCar_test\chassis_offline_remote.py` 已接入 MCU CAN2 指令，按用户要求默认 **500 rpm、30°**；P 上电、E 中立接管、WASD 目标、空格停机撤管、O 下电、R 下电后复位故障、Esc 停机下电退出。失焦撤管，MCU 租约 300 ms；电脑不再在正常控制路径直接写 CANopen。

10:23 安装 0.2.63，App/启动状态 A 逐字节一致，Boot、状态 B、恢复页与 Config V5 不变；见[安装索引](operations/evidence/2026-10-20-car2-install-0263-102312/installation.index.json)。10:24 经 CAN 分析仪通道 2 实际获得 `0x521/0x522/0x523` 同序号三帧响应，[CAN2 只读记录](operations/evidence/2026-10-20-car2-can2-status-102426/index.json)确认链路已打通。随后通过同一路径上电并只发送 ARM/零 rpm/零转角目标，执行器完成 RAM 软限位写入/复读，但随后转向状态字 `0x22BB` 出现故障位，控制撤销并已下电；见[零目标原始记录](operations/evidence/2026-10-20-car2-can2-neutral-102532/index.json)和[故障 RAM](operations/evidence/2026-10-20-car2-can2-neutral-fault-102636/index.json)。零目标检查失败，不是运动验收通过，尚未开放键盘测试。

故障后 CAN1 环形队列保留 Node 1 的 `0x181 = BB 22 04 00 6C 04 01`，见[原始队列](operations/evidence/2026-10-20-car2-can1-ring-103105/index.json)。若该帧第 2–3 字节按厂商示例映射为 2000h，则 `0x0004` 对应反馈错误 HallErr；当前只核验了 PDO 第一项 6041h，尚未读取第二映射项，报文字段含义仍为推断。操作者随后明确确认驱动器显示 HallErr/反馈错误，反馈故障得到现场确认，具体接线或反馈类型配置原因未作参数验证。反馈插头检查及后续恢复结果见下段；先前失败原始证据保留。

11:04 操作者确认断电检查反馈插头后 HallErr 消失。CAN2 清除下电状态下的旧锁存、重新上电后，ARM 与零目标持续 5 秒无先前驱动故障；立即 STOP/OFF 中断尚未完成的执行器停机，产生新的保护锁存，见[首次恢复记录](operations/evidence/2026-10-20-car2-can2-recovery-110402/index.json)。只读故障快照中转向状态为 `0x0637`、无驱动故障位，执行器 unsafe；下电后运行坐标失效，显示约 -2.47°／0.018° 不能作为同一运行坐标下的有效转角偏差，见[停机快照](operations/evidence/2026-10-20-car2-recovery-stop-fault-110649/index.json)。11:07 改为 STOP 后等待 1.5 秒再 OFF，完整零目标复测及最终无锁存下电检查通过，见[通过记录](operations/evidence/2026-10-20-car2-can2-recovery-110719/index.json)。这证明本次使能及停机检查通过，尚不证明 30° 转向或 500 rpm 行走。已打开原脚本的独立键盘终端（500 rpm／30°），等待操作者观察 A/D 实际动作；正常结束先空格、等待 2 秒、再 O，立即断动力仍保留保护行为。

随后在独立终端使用原入口完成 A/D 键盘测试：目标为 ±30°，驱动器反馈范围约 -28.768°～29.886°，独立编码器约 -28.687°～30.063°，两轮实测均为 0 rpm；操作者明确确认前轮机械动作正常、松键回中正常，转向已完成本车现场核验。该轮会话随后在已接管状态收到 P 上电请求，MCU 返回 owner busy，电脑按既有错误策略退出下电；见[键盘原始记录](operations/evidence/2026-10-20-car2-keyboard-field-110844/index.json)，不归为 HallErr。电脑入口已增加接管期间重复 P 只提示的处理，104 项主机测试通过；固件与正式包未改。已重开终端继续 W/S 500 rpm 正反转测试，行走与最终停机状态仍待确认。

操作者随后报告驱动轮不能正常运动，并说明原脚本可以正常驱动。该次[键盘记录](operations/evidence/2026-10-20-car2-keyboard-field-111637/index.json)为旧锁存 `flags=0x0A` 下 ARM 被安全门禁拒绝，没有任何 TARGET 或 ±500 rpm 指令，不能据此定位驱动器执行故障。随后清除锁存进行[15 秒零目标检查](operations/evidence/2026-10-20-car2-can2-recovery-112230/index.json)，配合[只读 SWD](operations/evidence/2026-10-20-car2-keyboard-observe-112240/index.json)确认驱动执行器 ACTIVE/step12/fault0，STOP 后 IDLE/step25，最后下电无锁存。原 PC 路径与 MCU 的 NMT、6060=3、零目标、6040=6/7/15、左右目标符号一致；模式复核对象 PC 为 6060、MCU 为 6061，本次 MCU 已完成初始化，尚无证据将该差异归为当前原因。未据此修改固件。已打开上电反馈就绪的新终端，等待 E 后实际 W/S 测试；尚未验收行走。

此前 09:49 安装 0.2.62 并完成启动维护，证据见[先前安装索引](operations/evidence/2026-10-20-car2-install-0262-094818/installation.index.json)。该版本只修复零限位，未提供本次电脑键盘所需 CAN2 控制入口，不能作为当前遥控固件。

修复针对正常已标定转向初始化遇到驱动器 `0x201A/0x2019=0/0` 的已知空限位：允许进入现有运行坐标下的 ±30.5° RAM 写入/复读；其余倒置或非零等值限位、已缓存限位复查变零仍拒绝，标定点动的保留限位路径不变，双反馈、停车、换向及 OTA 门禁不变。上电后只读快照为双节点在线、两路 0°、轮速 0、反馈门禁 0、运行中位 `-1463`（本次坐标），Flash 标定中位仍为 `-1464`；两者按运行坐标模型区分。

此前操作者按机械中位直接提交后，只读 SWD 已确认 Config V5 `calibrated=true`、驱动器中位 `-1464`、独立编码器中位 `0`，故障锁存清除，标定及转向执行器均回到 IDLE；见[标定后设备读回](operations/evidence/2026-10-20-car2-after-center-093417/index.json)。这是设备持久化结果，不是平台审计记录：平台认证接口返回 401，未读取本次操作 ID 或状态。09:37 安装前曾因电源输出命令开启而暂停，见[安装前检查](operations/evidence/2026-10-20-car2-preflash-0262-093737/index.json)；此后用户确认网页下电及 SWD 接好，09:48 重新核验全部停机条件并备份成功后才安装。网页键盘链路优先复用联网固件现有安全控制；CAN2 透明转发并非当前现场链路。

操作者随后提供的 09:38–09:40 网页截图显示：输出高电平、远控门禁就绪、stopped、已标定、反馈新鲜、左右 0 rpm、两路 0°、无故障锁存，遥控页显示已回中，但键盘按钮灰色且仍需二次确认接管。操作记录截图在 09:36:36 和 09:36:42 有 `device_operation_completed`，其余可见多项 created；未展开命令、结果与操作 ID，不能将其中某行指定为本次中位提交。截图是对应时刻的平台呈现，不是实时运动许可；已指明设备控制台右侧红色“车辆下电”按钮，等待操作者下电确认后再继续安装。

后续停机状态复查未成功：探针 VTref 为 3.304 V，但 DAP 无法连接；没有取得新 RAM/Flash 数据，也没有烧录。J-Link 虽只提交 read/save 命令，连接失败后自动尝试了 connect-under-reset 与复位引脚回退，不能描述为全程无复位尝试。见[失败连接记录](operations/evidence/2026-10-20-car2-stop-recheck-094255/index.json)。截断输出最初被误读为 0.004 V，已按完整日志增加[更正说明](operations/evidence/2026-10-20-car2-stop-recheck-094255/voltage-correction.json)，原始日志和索引不改写。待确认 SWD 连接、车辆输出保持关闭后再连接。

此前失败的准确诊断为 `phase=4, step=2, fault=8, preserve_limits=1`，驱动位置约 `-1464`，限位下界读为 0，点动路径在写入运动目标前拒绝；全局 last SDO Abort `0x06020000` 不能单独归为本次限位读取失败。精确匹配现场 App 的 ARM ELF 和 ABI 偏移已复现，见[只读 RAM 证据](operations/evidence/2026-10-20-car2-ram-readonly-092202/index.json)。早先 Config/故障记录是提交前快照，见[只读配置及故障索引](operations/evidence/2026-10-20-car2-config-fault-readonly-091327/index.json)。含凭据配置只保留在 Git 忽略的 `out/flash/`；旧证据与哈希不改写。

同次用户明确保留联网固件是为了在设备运维平台给第二台车做中位标定，现阶段保持该固件。只读核对已部署的 `features/chassis_control.js`，确认网页现有 W/S、A/D、空格键盘绑定，要求 active 接管会话且不在输入控件/对话框内；标定提交检查设备 `DONE`、`calibrated=true` 和 `profileCompatible=true`，失焦/隐藏/离线/切页结束控制。公开静态代码及采集哈希保存在 `out/logs/chassis-web-keyboard-readonly-20261020/`；未登录或操作用户平台会话，未读取当前设备实时状态，也未提交标定、接管或运动请求。下一步先取得本车标定终态及双角度读回，再核验是否直接采用网页现有键盘链路，或最终仍要求 USB-CAN 离线控制；真实转向和键盘遥控仍未验收。当前执行路径见[联网标定与网页键盘](operations/巡防车底盘逐车标定与参数记录.md#联网标定与网页键盘)。

2026-10-20 后续现场核对：操作者确认当前为第二台车、已架空、急停可用，主控及驱动器已上电、CANalyst 物理通道 2 接 A-BOX CAN2、CAN1 接驱动器，并人工确认车轮机械中位。复查 Node 1 的 12 项及 Node 2 的 7 项只读对象仍全部超时；[原始记录](operations/evidence/2026-10-20-car2-centered-readonly-090430/index.json)没有得到本次 `6064` 或限位，不能从人工摆正推导计数。此前[3 秒硬件只听采集](operations/evidence/2026-10-20-keyboard-passive-090318/index.json)也未收到 CAN 帧。随后只读 SWD 检查 VTOR 为 `0x08008000`，读 App 区域的前 148,520 字节与当时 dist `0.2.61` 的 App 逐字节一致，和本地 `OfflineRemote` 不同；[SWD 原始记录与镜像哈希](operations/evidence/2026-10-20-car2-swd-readonly-090620/index.json)已归档。该正式包构建提交的 `patrol_rtos.c` 没有 CAN2 透传实现，现车并未运行电脑离线脚本所要求的网关，这是当前 CAN2 直发驱动器 SDO 路径不成立的直接原因。SWD 未发送 halt/reset/run、擦除、烧录或写内存命令，DHCSR 两次读取均未显示 S_HALT；未向车辆写限位或发运动目标。下一步须统一电脑与 MCU 控制路径：若必须离线，则需接入 MCU 本地安全控制入口并保留双反馈、运行坐标、换向及失联租约保护；若允许联网，则复用正式远控链路。当前仍未恢复真实转向或键盘遥控，不能直接烧入绕过运动安全仲裁的透明网关便宣称完成。

2026-10-20 继续排查电脑键盘转向：CANalyst-II 通道 2 可以打开，但 Node 1 的 12 项 SDO 只读请求全部超时，本次不能确认节点在线或零限位仍存在；原始结果见[只读索引](operations/evidence/2026-10-20-keyboard-steering-085422/index.json)。外部 `E:\1_work\ChassisCar_test\chassis_offline_remote.py` 已增加独立 RAM 限位维护入口，必须显式输入本次已确认运行中位，先零速、三对实测停车、撤使能及故障核验，再逐项写入并复读 ±30.5° RAM 限位，不使能、不发位置目标、不写 EEP。键盘换向保留上次非零方向，修复松键后反向跳过零速确认及等待后执行已松开请求的问题；按键不变时持续检查驱动反馈及厂商故障。默认目标收紧到 50 rpm/5°。99 项纯主机回归及限位维护模拟通过，摘要与外部源文件哈希见 `out/logs/keyboard-steering-verification-20261020.json`。本次没有向真实车辆写参数、发运动命令、改固件或烧录，dist 不变。独立编码器、本次上电坐标重建、MCU OTA/Bus-Off 和硬件侧租约保护尚未接入，不能据限位维护或主机测试宣称键盘遥控已经可用于整车运动。当前仍待确认本车身份、架空/急停与坐标，恢复通信后核验限位及真实小角度转向；操作边界见[逐车流程](operations/巡防车底盘逐车标定与参数记录.md)。

2026-10-19 CAN2 线下键盘转向排查：通过 CANalyst-II 通道 2 只读 Node 1，确认 `201A=0`、`2019=0`，`6064=-1259`、`607A=0`，位置模式为 1、`603F/1001=0`。外部脚本 `E:\1_work\ChassisCar_test\chassis_offline_remote.py` 原先把所有左右目标按该零宽度范围裁剪为 0，SDO ACK 不代表执行了左右定位。脚本已增加无效限位/位置越界拒绝、实际目标打印、只读诊断及启动失败独立零速/撤使能处理；14 项纯主机回归通过。原始只读回读及哈希见[本次索引](operations/evidence/2026-10-19-offline-steering/index.json)。本次未写驱动器参数、未发运动命令或烧录；当前机械中位、有效限位恢复及真实转向仍待核验。使用既有[逐车标定流程](operations/巡防车底盘逐车标定与参数记录.md)核对本车，不能将本次当前位置作为机械中位；本次不改变正式包状态。

同日线下脚本增加显式 `--car 1..4` 和同目录 `car_profiles.json`：四车共用控制路径，CAR1/CAR2 记录中位 `-83815`/`-1463`，CAR3/CAR4 未登记，正常控制拒绝；缺参数或无效档案也在打开 CAN 前拒绝。第三、第四台保留 `null` 与独立 TODO 提示，补充字段含义及 `--car 3/4 --show-car-profile` 离线查看入口，该入口不加载 DLL、不打开 CAN。启动实测位置与档案中位分开保存，启动保持实测位置，后续手动目标使用选中参考；退出清理不回中。只读诊断不依赖车辆档案。43 项模拟总线及档案主机测试通过，包括 CAR3/CAR4 使用临时测试中位的启动、手动目标、停机及无效限位拒绝；正式电脑档案仍保留未知值，与当前固件车辆表一致。未访问真实 CAN、未发运动或参数写入、未编译或烧录固件。使用入口及未完成的当前坐标核验、双反馈门禁、RAM 限位恢复和上电回中见[逐车操作说明](operations/巡防车底盘逐车标定与参数记录.md)。本次只完成脚本选车及占位入口，不代表已恢复真实转向或通过四车现场验收，正式包状态不变。

同日新增外部只读工具 `E:\1_work\ChassisCar_test\chassis_can_remote_monitor.py`：CANalyst 使用硬件只听模式，独立接收原始帧、分别统计各 ID 新鲜度，并标明 Excel 与参考程序两套协议候选；可保存完整 JSONL。CAN2 并联抓包只证明总线有帧；独立 CAN1 段可核对已启用网关的 A-BOX 接收转发，仍不能作为业务解析、无线连接或运动成功证据。33 项禁止真实 DLL/发送 API 的主机测试及离线演示通过；本次未访问真实 CAN、未修改固件或烧录，正式包不变。接线及运行命令见[只读观察流程](operations/巡防车底盘逐车标定与参数记录.md)。

2026-10-14 历史快照：当时源码、正式包和本车现场固件为 `patrol-chassis-0.2.61`。回中后撤使能确认超时已按 MCU/CAN 时序修复，平台 `0.4.185` 已接入接管确认中的下电复位与新状态等待。修复后的接管、续租、结束会话、本地回中和再次接管均已实际验证，最后保持下电且 faultLatched=false。完整证据见[本次索引](operations/evidence/2026-10-14-can-stop-fix/index.json)。Config V5 与公共 MQTT V4 保持冻结；跨重启完整动作账本仍未接入。

## 当前正式包

2026-10-20 正式包为 `patrol-chassis-0.2.63`，App SHA256 `7DD0B02296020CF477D9D36C8E1DA782AA5DA430DF3CC022430E66D1E9E606E5`，149,936 字节，CRC32 `1EF10377`。源码提交 `68455ecda51816355475b2cf54dfccfbcc784773`（本地分支 `fix/can2-keyboard-0263`），公共平台固定 `bc64f52b290521f53ececa626dc91765861845f8`。42 项产品 Python 回归含实际 CAN2/RemoteService/ChassisControl 联合 C 夹具通过；103 项电脑端回归通过，含三帧关联、反馈过期/拒绝、失焦、重接管和 500 rpm/30° 默认值。正式 ARM 构建、布局、公共服务栈及新增 CAN2 任务栈检查通过；CAN2 目标/复位/响应/租约停机已知帧分别为 288/352/192/128 字节，加 512 字节保留量均不超过各自 1024 字节任务栈。保留 ELF 重建与正式 App 哈希一致。冻结 Boot、RAM 覆盖 61,432 字节及配置/状态页边界不变。暂存校验后经 `tools/artifact_store.py` 整体替换 dist，旧 0.2.62 正式包见[替换前归档](operations/evidence/2026-10-20-before-0263-release-101954/index.json)。现场已安装并证实 CAN2 通信；首次零目标启用时 HallErr 已经操作者检查反馈插头后消失，后续 5 秒零目标与完整停机下电检查通过。真实转向和键盘行走尚未验收，不能据正式打包声称硬件恢复。

以下为 2026-10-14 的 0.2.61 历史现场快照，不作为本次已完成运动验收的证明。

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
