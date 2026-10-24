# Node 1 位置 TPDO/SDO 调度候选

版本：`patrol-chassis-0.2.84`

变更：

- `0x281` TPDO 在 100 ms 内成功更新时，`0x6064` SDO 改为 250 ms 一次兜底读取。
- `0x281` 超过 100 ms 未更新时，`0x6064` 立即恢复 50 ms SDO 轮询。
- TPDO 与 SDO 使用独立来源时间，SDO 兜底读取不会延长 TPDO 新鲜时间。

验证：

- TPDO 新鲜、250 ms 兜底周期、TPDO 超过 100 ms 后提前恢复 SDO、畸形 DLC 和错误节点用例通过。
- ARM Release、栈、CAN2 栈、Flash 布局和冻结 Boot 检查通过。
- App 154,520 字节，RAM 60,904 字节。
- 产品 Python 回归保持 43/45，两个失败为既有 `profile == 90` 断言和热复位静态检查。

烧录：

- `Patrol_Vehicle_Chassis_App.bin -> 0x08008000`
- `BootState_A.bin -> 0x0803E800`
- `BootState_B.bin -> 0x0803F000`
- 不修改 `0x0803E000` 恢复页和 `0x0803F800` Config V5。

App SHA256：`14A97169DC14145F717806225A82FC591F593A1FEB911992AE72556FD7EA87C1`

本候选尚未烧录，尚未进行现场运动验收。
