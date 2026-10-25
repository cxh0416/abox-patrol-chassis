# CAN2 本地中位标定候选

日期：2026-10-24  
候选版本：`patrol-chassis-0.2.86`  
用途：在 4G/网页平台不可用时，通过 CAN2 执行与网页相同的中位标定状态机。

## 实现边界

- 新增 CAN2 请求 `0x530` 与三帧响应 `0x531/0x532/0x533`。
- `START/JOG/COMMIT/CANCEL` 直接调用产品原有 `CenterCalibration_*`，不绕过稳定反馈、安全门禁、编码器三次零点验证或 Config V5 保存读回。
- 本地 CAN2 会话使用短期令牌、递增序号和 3 秒通信期限；超时或 Bus-Off 会请求取消。
- 不直接影响其他传输创建的标定会话。
- 电脑端脚本只通过 MCU 维护协议工作，不直接向 CAN1 驱动器写参数。

操作入口见[本地中位标定工具](../../../../can2_center_calibration/README.md)和[协议](../../../protocols/巡防车底盘本地CAN控制协议.md)。

## 烧录

写入以下三个文件并全部成功校验后再复位：

1. `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`，188,368 字节。
2. `BootState_A.bin` → `0x0803E800`，2,048 字节。
3. `BootState_B.bin` → `0x0803F000`，2,048 字节。

如已逐字节确认冻结 Boot 匹配，可改刷：

- `Patrol_Vehicle_Chassis_App.bin` → `0x08008000`，155,600 字节。

禁止整片擦除。必须保留 `0x0803E000..0x0803E7FF` 恢复页和 `0x0803F800..0x0803FFFF` Config V5/故障记录页。

App SHA256：`84590b4bbe63f16a917409d611137669c712c6ea68e21c4de3c2247bd1043968`  
Full SHA256：`a3218985380b0c9b36c39ca0fd34beed3572800ac45ea5959946518009567c22`

## 验证

- ARM Release 构建通过；App 155,600 字节。
- 产品 CAN2 栈和标定栈边界检查通过。
- Boot State A/B 实际解析通过，State A 绑定本 App 大小、CRC 和版本。
- CAN2 主机回归覆盖 START、令牌、JOG、COMMIT、CANCEL、序号、过期和“不影响其他标定传输”。
- 电脑端协议/一键当前中位流程使用假总线回归通过。
- 产品 Python 回归 46/47；唯一失败为既有的 `test_identity_and_safe_defaults` 静态断言，与本功能无关。

## 现场状态

本记录生成时尚未将 App 烧录到当前车辆，也未执行真实编码器置零、Config V5 写入或完整断电重启验收。现场动作必须另行确认车轮机械中位、架空/急停条件及完整断电后的读回结果。
