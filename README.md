# 巡防车底盘控制器

产品标识固定为 `patrol_vehicle_chassis`。当前正式固件与源码版本均为 **0.2.21**；正式产物及哈希以 `dist/release.manifest.json` 为准。主机测试、构建和制品发布不等同于台架或实车验收。

0.2.21 正式包采用 4096 count 单圈编码器、固定中位 2048、±30° 转向范围和 ±200 rpm 行走目标限制，并增加新车首次建标的独占低速点动；运动准入门与转向坐标纪元失效判定已经分离，避免标定点动完成后的短暂双驱状态过期把空闲转向执行器误锁存为故障，同时避免正常取消标定并下电后，节点的迟到 Boot-up 心跳把已经清除的空闲坐标误锁存为执行故障。转向和行走仍受驱动器软限位、双传感器一致性、反馈新鲜度及其余安全门禁约束，启动默认不授予运动许可。真实角度、电源、异常掉电恢复及道路能力以对应现场证据为准。

## 固定边界

- Boot：`0x08000000..0x08007FFF`；App：`0x08008000..0x0803E7FF`。
- Boot State A/B：`0x0803E800`、`0x0803F000`；配置页：`0x0803F800..0x0803FFFF`。
- 平台子模块固定到 `e60057c9639238c47fdc168cf2eed81594a1d034`；只复用平台基础设施。
- 换向先发送双零速，连续三次左右新反馈绝对值 ≤20 rpm，2 秒超时撤使能并锁存。
- 未标定、反馈过期、双角度偏差超 2°、节点离线、Bus-Off、驱动故障、OTA 忙及编码器配置/范围异常时禁止运动。
- 未确认硬件输入不绑定，PA0 命令状态不能代表接触器或整车供电反馈。

## 构建与维护

```powershell
# 默认读取仓库根目录 VERSION；临时候选也可显式传 -AppVersion 覆盖。
.\tools\run_tests.ps1
.\tools\build_bench_steering.ps1
.\tools\build_bench_steering.ps1 -Drive
.\tools\build_bench_steering.ps1 -Drive -AppVersion patrol-chassis-0.2.17
python tools/clean_workspace.py --build
```

当前源码的构建版本默认值只在仓库根目录 `VERSION` 维护；CMake 和 `tools/` 下的构建脚本默认读取该文件，`-AppVersion` 仅用于显式覆盖。纯转向和行走候选分别输出到 `out/candidates/bench-steering/`、`out/candidates/bench-drive/`；每类只保留最近成功构建。在线调试包使用 `out/debug/online/`，构建与主机测试日志放 `out/logs/`。

`dist/` 仅保留最新正式版的 Boot、App、Full、发布清单四个文件。正式打包须满足干净工作树及已验证候选一致性门禁，先在暂存目录完成文件集合、版本、向量、长度、CRC32、SHA256、Full 内容和分区校验，再整体替换；失败恢复原包。发布脚本不发起 OTA。

清理命令默认只预览；加 `--apply` 才执行，加 `--build` 才清除 `app/build/`。脚本保护 `dist/`、当前候选、调试包、平台冻结 Boot 和持久证据，并拒绝越界目录及目录联接。旧 `out/` 的现场记录已保存到 [压缩包](docs/operations/evidence/raw/2026-09-09-out.zip)，原路径、成员名和 SHA256 见 [索引](docs/operations/evidence/raw/2026-09-09-out.index.json)。

## 文档入口

- [需求与验收](docs/requirements/首阶段需求与验收.md)：当前候选能力、待现场验证项目及早期阶段边界。
- [公共业务协议](docs/protocols/A-BOX车辆底盘业务协议-V3.0.md)：对外身份、Topic、命令和状态语义。
- [内部运维协议](docs/operations/巡防车底盘A-BOX内部运维协议-1.0.md)与 [CANopen 执行约束](docs/operations/CANopen执行约束.md)：内部接口和产品实现约束。
- [逐车标定](docs/operations/巡防车底盘逐车标定与参数记录.md)：当前候选流程和记录模板。
- [硬件与安全边界](docs/hardware/硬件与安全边界.md)：本产品端口、分区和待确认硬件边界。
