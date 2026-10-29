# patrol-chassis-0.2.101 当前源码构建烧录包

本包由当前工作树重新执行 ARM Release 构建生成。正常动态转向以驱动器位置反馈为主；独立编码器只负责中位标定、输出轴绝对范围，以及自动回中和标定边界上的 `2°`一致性校验。

## 修改

- 正常遥控/CAN 运动和实体遥控转向不逐帧比较两路角度，也不要求角度样本配对。
- 独立编码器继续参与在线、配置、单圈规格和输出轴 `±30.5°`绝对范围门禁。
- 自动回中开始前执行一次性配对及 `2°`检查；配对无效时等待，真实超差时锁存 `ANGLE_MISMATCH`。
- 自动回中完成后要求两路反馈配对有效、均进入中位并满足 `2°`一致性。
- 中位标定继续使用两路相对变化差值进行 `2°`校验。
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
| `Patrol_Vehicle_Chassis_Full.bin` | 189,612 B | `D5F87EDE06F32FA4333B7B4C84C974FB7BE79EDB4694E315CF9914D84E4E1146` |
| `Patrol_Vehicle_Chassis_App.bin` | 156,844 B | `86D04A4317D12595F4988204617D1D0ACAFBA2041DABE7E384DB38E95AD2CB9C` |
| `BootState_A.bin` | 2,048 B | `D8CDB4957149FCE4759ED0360E283937975C92DFAA6B2EDA27BA8B88CB25269F` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`B74D7367`
- Full CRC32：`C05B88FF`
- State A record CRC32：`D9FBDA9F`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2 栈、Bench 栈、`git diff --check`、Full 拼接、版本标记、App 向量及真实 Boot State V3 解析/CRC 检查通过。全量主机回归为 47/56，失败项为 1 项源码静态规则、7 项历史证据链接和 `test_production_stop_arbitration`。因此本包是证据候选，不能作为完整回归通过或现场验收结论。

尚未烧录或实车验证。烧录前须确认 `test_production_stop_arbitration` 失败对应的停止路径是否符合当前预期，并保持车辆架空、硬件急停可用。
