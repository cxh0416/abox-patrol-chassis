# patrol-chassis-0.2.100 双角度门禁边界化烧录包

本包将独立绝对编码器从正常动态转向门禁中移出。实时转向只使用驱动器电机位置反馈；独立编码器只负责中位标定、输出轴绝对范围，以及自动回中和标定边界上的 `2°`一致性校验。

## 修改

- 正常遥控/CAN 运动和实体遥控换向中位过渡不再要求 `angle_pair_valid`，也不逐帧比较两路角度。
- 独立编码器继续参与在线、配置、单圈规格和输出轴 `±30.5°`绝对范围门禁。
- 自动回中开始前执行一次性配对及 `2°`检查；配对无效时等待，真实超差时锁存 `ANGLE_MISMATCH`。
- 自动回中完成后要求两路反馈配对有效、均进入中位并满足 `2°`一致性。
- 中位标定继续使用两路相对变化差值进行 `2°`校验。
- 实体遥控转向目标限制为 `12000 mdeg/s`，约 `12°/s`。
- 目标跨过中位且变化达到 `5000 mdeg` 时，先回中；实时中位完成判断只使用电机位置，`±1000 mdeg` 连续 3 帧稳定后，再转向新目标。
- 独立绝对编码器运行期轮询保持 `20 ms`；未恢复 Node 1 高频 `0x6041/0x6064` SDO 轮询。

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
| `Patrol_Vehicle_Chassis_Full.bin` | 190,316 B | `6713817166AD8F6D9CB01981C2F0E931B695D0B7C969A7D329148FD272C34E62` |
| `Patrol_Vehicle_Chassis_App.bin` | 157,548 B | `3C0C2D73E6F589AE377EC0D5442046101300B94B7AEB925F6F6D1B53EE1D858A` |
| `BootState_A.bin` | 2,048 B | `04733D3C7A7A7C69CBB05DEAED4B2A9678BD7806CB4623AA45D2687EEDEA2F1D` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`7B65E612`
- Full CRC32：`FE82322A`
- State A record CRC32：`8B4F5DCE`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2 栈、Bench 栈、`git diff --check`、Full 拼接、版本标记、App 向量及真实 Boot State V3 解析/CRC 检查通过。全量主机回归为 49/57；8 项失败与修改前相同，为 1 项既有源码静态规则和 7 项历史证据链接失效。

尚未烧录或实车验证。现场应确认正常快速/缓慢转向不再因两路异步角度误锁存；断电重启后自动回中能完成且无执行故障；自动回中开始前若两路真实超差仍锁存 `ANGLE_MISMATCH`；独立编码器离线或输出轴绝对范围超限仍阻止运动。
