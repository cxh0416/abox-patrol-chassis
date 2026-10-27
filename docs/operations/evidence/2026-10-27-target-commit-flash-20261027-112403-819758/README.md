# patrol-chassis-0.2.102 转向目标提交与触发间隔烧录包

本包由当前工作树重新执行 ARM Release 构建生成。正常动态转向以驱动器位置反馈为主；独立编码器只负责中位标定、输出轴绝对范围，以及自动回中和标定边界上的 `2°`一致性校验。

## 修改

- 删除`0x607A`写入后在`0x3F`触发前的目标回退覆盖。
- 新遥控目标只保留在单槽最新目标中；当前`0x607A -> 0x3F -> 0x2F`周期完成后才处理。
- 已发送的定位触发不取消、不重放、不被新目标覆盖。
- 两次`0x6040=0x3F`触发起始时间至少间隔`100 ms`。
- 正常遥控/CAN 运动和实体遥控转向不逐帧比较两路角度，也不要求角度样本配对。
- 独立编码器继续参与在线、配置、单圈规格和输出轴 `±30.5°`绝对范围门禁。
- 自动回中开始前执行一次性配对及 `2°`检查；配对无效时等待，真实超差时锁存 `ANGLE_MISMATCH`。
- 自动回中完成后要求两路反馈配对有效、均进入中位并满足 `2°`一致性。
- 中位标定继续使用两路相对变化差值进行 `2°`校验。
- 当前转向 Profile 的最大、中速和慢速档均设置为 `1000 rpm`，`0x6080/0x6081` 使用该值。
- 保持当前源码中的转向速度、行走上限、换向零速确认和故障停机流程。

## 烧录

- `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
- `BootState_A.bin` → `0x0803E800`
- `BootState_B.bin` → `0x0803F000`

已有匹配冻结 Boot 时，可用 `Patrol_Vehicle_Chassis_App.bin` → `0x08008000` 替代 Full，但仍须写入本包配套 State A/B。

不要整片擦除；保留：

- `0x0803E000..0x0803E7FF` 配置恢复页
- `0x0803F800..0x0803FFFF` Config V5 与故障日志页

## 校验

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `Patrol_Vehicle_Chassis_Full.bin` | 189,596 B | `43210984F9A75787F8E1A5ABAC0B49D878A1247B8606C3D37287FC24AB117F49` |
| `Patrol_Vehicle_Chassis_App.bin` | 156,828 B | `4E9B3572E43A3DEE3FA3BECD6CFF6EEA674F8B0E0F73CC3474BEC1E9F97F8A15` |
| `BootState_A.bin` | 2,048 B | `D75C291C2FFD01BCF6A3887F1902F4E5E9EAB5B79D210D2B4650625FB99875D9` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`5D8E248C`
- Full CRC32：`51342170`
- State A record CRC32：`9BCD63BE`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2 栈、Bench 栈、`git diff --check`、Full 拼接、版本标记、App 向量及真实 Boot State V3 解析/CRC 检查通过。全量主机回归为 47/56，失败项为 1 项源码静态规则、7 项历史证据链接和 `test_production_stop_arbitration`。因此本包是证据候选，不能作为完整回归通过或现场验收结论。

尚未烧录或实车验证。烧录前须确认 `test_production_stop_arbitration` 失败对应的停止路径是否符合当前预期，并保持车辆架空、硬件急停可用。
