# 巡防车底盘控制器

`patrol_vehicle_chassis` 是巡防车底盘 A-Box 固件，当前版本 `0.2.13`。本仓库只复用已提交的 STM32F105、FreeRTOS、EC800、MQTT 和 Boot V2 基础设施，不包含清扫车业务。

当前阶段为架空低速测试：同车型共用比例和方向，每车保存中位；独立编码器提供轮胎角度基准，上电静止建立驱动器 RAM 坐标。测试必须通过内部 RAM 许可和公共控制租约，默认不使能；独立行走许可支持 ±50 rpm 目标，前后方向留待现场确认。转向、行走、停止与保护记录见内部实现文档；整车接触器反馈和本地安全输入尚未验收。

## 关键边界

- Boot：`0x08000000..0x08007FFF`
- App：`0x08008000..0x0803E7FF`
- Boot State A/B：`0x0803E800`、`0x0803F000`
- 产品配置页：`0x0803F800..0x0803FFFF`
- 产品标识固定为 `patrol_vehicle_chassis`
- 平台子模块固定到 `e60057c9639238c47fdc168cf2eed81594a1d034`
- `dist/` 只保存最新正式版，正式打包会整体替换目录

## 构建与测试

```powershell
.\tools\run_tests.ps1
cmake --preset Debug -S .\app
cmake --build .\app\build\Debug
```

正式产物由 `.\tools\build_release.ps1` 生成 App、Full 和清单。发布脚本不会发起设备 OTA。

台架候选由 `.\tools\build_bench_steering.ps1` 生成到 `out/`。完成实测后，使用 `build_release.ps1 -BenchSteering -VerifiedCandidate <已验证App.bin>` 整体生成正式产物；脚本要求正式构建与候选 SHA256 完全一致才替换 `dist/`。通用运动编译开关始终关闭。

## 文档

- `docs/operations/架空行走与平台远控实现.md`：架空行走执行、换向实测与平台入口；行走正式包使用 `build_release.ps1 -BenchDrive -VerifiedCandidate <已验证App.bin>`。

- `docs/archive/2026-08-31-input/`：原始输入，原样归档并记录哈希
- `docs/protocols/CANopen执行约束.md`：厂商 CANopen 的产品化执行约束
- `docs/protocols/A-BOX车辆底盘业务协议-V3.0.md`：公共平台 V3 底盘业务 Profile，固定 `deviceType=vehicle_chassis`
- `docs/protocols/巡防车底盘A-BOX内部运维协议-1.0.md`：设备信息、日志、配置和 OTA 的内部 V2 协议，响应固定携带 `product`
- `docs/hardware/硬件与安全边界.md`：端口、分区和未确认硬件边界
- `docs/requirements/首阶段需求与验收.md`：分阶段范围与验收门禁
- `docs/operations/2026-09-02-CAN只读联调.md`：现场 SDO 短帧问题、修复与验收证据
- `docs/operations/车型参数与台架转向实现.md`：公共标定、上电坐标同步、首轮转向与保护测试
- `docs/references/RTBOX参考边界.md`：仅记录可借鉴状态机，不复制引脚和协议
