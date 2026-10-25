# CAN2 中位标定上电前抑制自动回中

日期：2026-10-24  
候选版本：`patrol-chassis-0.2.87`

## 修复原因

`0.2.86` 的电脑端脚本先发送 `POWER_ON`，然后等待操作者输入 `YES`。在等待期间，本地中位标定会话尚未建立，运行层的自动回中会使用 Config V5 中旧中位 `-83815`，而当前实际转向位置不同，导致转向轮在确认提示出现前被驱动到约 30°。

## 修复

- CAN2 标定增加 `PREPARE` 操作，在不下电、不运动的情况下暂时抑制自动回中。
- 脚本调整为先确认机械中位，再发送 `PREPARE`，随后上电并建立标定会话。
- `START` 只有在有效 `PREPARE` 窗口内才能执行；成功后立即清除该窗口。
- 新增活性检查：存在 `phy-remote` 等其他控制会话时，脚本在显示 `YES` 前直接拒绝。
- 新增独立 `reset-fault` 操作；清除后再次读取 motion flags，确认 bit 3 已消失。
- 一键清除故障和完整标定脚本仍只调用 `CenterCalibration_*` 与 `RESET_FAULT`，不直接写驱动参数。

协议见 `docs/protocols/巡防车底盘本地CAN控制协议.md`，工具见 `can2_center_calibration/`。

## 构建与烧录

- App：155,760 字节，CRC32 `9DB7B0ED`，SHA256 `2757cbc80c20e0952be8ba0182534c3408d6564ca261a8c915d84300b957d814`。
- Full：188,528 字节，SHA256 `2f746716d5a447c274471e270700813aa7413f39414886ed37cda9ba6b4a1c96`。
- Full → `0x08000000`。
- BootState A → `0x0803E800`。
- BootState B → `0x0803F000`。
- 禁止整片擦除，保留 `0x0803E000..0x0803E7FF` 和 `0x0803F800..0x0803FFFF`。

## 验证

- ARM Release、服务栈、CAN2 栈和标定栈检查通过。
- Boot State V3 实际解析通过，State A 绑定本 App 大小、CRC 和版本；损坏 State/App 不跳转。
- CAN2 主机测试覆盖 PREPARE、START 前置条件、令牌、JOG、COMMIT、CANCEL、独立 RESET_FAULT 和活动会话阻断。
- 电脑端当前中位流程使用假总线回归通过。
- 产品 Python 回归 49/50；唯一失败仍为既有 `test_identity_and_safe_defaults` 静态断言。

本包尚未烧录；尚未在真实车辆验证“确认期间不自动回中”、编码器置零、Config V5 掉电保持及重启后的稳定性。
