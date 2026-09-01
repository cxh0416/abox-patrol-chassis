# 巡防车底盘控制器

`patrol_vehicle_chassis` 是巡防车底盘 A-Box 固件，当前版本 `0.2.0`。本仓库只复用已提交的 STM32F105、FreeRTOS、EC800、MQTT 和 Boot V2 基础设施，不包含清扫车业务。

当前阶段是“被动观测联调”：Release 固件只发送 CANopen SDO 读请求和绝对编码器 Modbus 读请求，解析节点心跳、实测转速、转向位置和编码器计数。运动执行仍由独立编译门禁保持关闭；构建成功、MQTT ACK 或 CAN 报文不代表车辆已经运动。

## 关键边界

- Boot：`0x08000000..0x08007FFF`
- App：`0x08008000..0x0803E7FF`
- Boot State A/B：`0x0803E800`、`0x0803F000`
- 产品配置页：`0x0803F800..0x0803FFFF`
- 产品标识固定为 `patrol_vehicle_chassis`
- 平台子模块固定到 `2c4b21ab6fd2084426512f387a12b8fbd7149b20`
- `dist/` 只保存最新正式版，正式打包会整体替换目录

## 构建与测试

```powershell
.\tools\run_tests.ps1
cmake --preset Debug -S .\app
cmake --build .\app\build\Debug
```

正式产物由 `.\tools\build_release.ps1` 生成 App、Full 和清单。发布脚本不会发起设备 OTA。

## 文档

- `docs/archive/2026-08-31-input/`：原始输入，原样归档并记录哈希
- `docs/protocols/CANopen执行约束.md`：厂商 CANopen 的产品化执行约束
- `docs/protocols/A-BOX车辆底盘业务协议-V3.0.md`：公共平台 V3 底盘业务 Profile，固定 `deviceType=vehicle_chassis`
- `docs/protocols/巡防车底盘A-BOX内部运维协议-1.0.md`：设备信息、日志、配置和 OTA 的内部 V2 协议，响应固定携带 `product`
- `docs/hardware/硬件与安全边界.md`：端口、分区和未确认硬件边界
- `docs/requirements/首阶段需求与验收.md`：分阶段范围与验收门禁
- `docs/references/RTBOX参考边界.md`：仅记录可借鉴状态机，不复制引脚和协议
