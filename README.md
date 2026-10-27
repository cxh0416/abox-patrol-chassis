# 巡防车底盘控制器

`patrol_vehicle_chassis` 是 STM32F105 + FreeRTOS 的巡防车底盘控制器。源码版本由根目录 `VERSION` 维护；最新正式固件、产物和哈希只以 `dist/release.manifest.json` 为准。构建成功、MQTT ACK 或 CAN 报文不等同于台架、实车或 OTA 现场验收。

## 固定边界

- Boot 从 `0x08000000` 开始，App 从 `0x08008000` 开始；Boot State A/B 和配置页边界不得改变。
- 换向前必须发送双零速，并连续三次取得左右实测转速绝对值不超过 20 rpm 的新反馈；6 秒超时撤销使能并锁存故障。
- 未标定、反馈过期、节点离线、CAN Bus-Off、驱动故障、编码器规格或范围异常、OTA 忙时禁止运动；普通运动取消双角实时 2° 比较，标定与自动回中边界校验保留。
- 未确认继电器、接触器反馈和本地按键引脚前，不得绑定硬件输出或把命令状态表述为实际供电状态。
- `dist/` 只保存最新正式版；候选、调试包、历史记录和原始证据不得混入正式制品目录。

## 构建与测试

```powershell
.\tools\run_tests.ps1
.\tools\build_bench_steering.ps1
.\tools\build_bench_steering.ps1 -Drive
python tools/clean_workspace.py --build
```

候选分别写入 `out/candidates/bench-steering/`、`out/candidates/bench-drive/`，在线调试包写入 `out/debug/online/`，日志写入 `out/logs/`。正式打包通过 `tools/build_release.ps1` 在暂存目录校验后整体替换 `dist/`；发布脚本不发起 OTA。清理命令默认只预览，显式加 `--apply` 才执行。

## 文档入口

1. [CURRENT](docs/CURRENT.md)：唯一当前状态、冻结测试基线、当前 blocker、下一步和权威文档入口。
2. [底盘业务 Profile](docs/protocols/A-BOX巡防车底盘业务协议-V4.0.md)与[内部运维接口 V2.3](docs/protocols/A-BOX内部运维协议V2.3.md)：目标产品接口合同。公共 MQTT V4 由平台仓定义；Profile 1.0、内部报文 2.0 和 Config V5 分别是不同的版本。现场切换状态见 [CURRENT](docs/CURRENT.md)。
3. [源码模块与依赖](docs/implementation/源码模块与依赖.md)与[硬件与安全边界](docs/hardware/硬件与安全边界.md)：当前实现和安全边界。
4. [当前运维说明](docs/operations/)：标定、CANopen、台架和维护流程。
5. [Evidence 索引](docs/operations/evidence/README.md)：需要证明结论时再进入原始证据。

已完成工作包和旧阶段记录位于 `docs/operations/history/`，不代表当前发布或当前操作步骤。
