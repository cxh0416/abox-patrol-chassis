# patrol-chassis-0.2.98 转向斜率、跨中位过渡与反馈时间对齐烧录包

本包针对遥控快速大幅换向时，独立编码器与驱动器位置反馈的时间偏差叠加机械动态变化，瞬时超过 `2°` 而锁存 `ANGLE_MISMATCH` 的问题。

## 修改

- 实体遥控转向目标限制为 `12000 mdeg/s`，约 `12°/s`。
- 目标跨过中位且变化达到 `5000 mdeg` 时，先回中；两路角度均在 `±1000 mdeg` 且连续 3 个有效帧稳定后，再转向新目标。
- 只有驱动器位置与独立编码器样本时间差不超过 `30 ms` 时，才比较两路角度差值。
- 两路角度未形成有效配对时返回不锁存的 `FEEDBACK_STALE`，拒绝新运动并禁止自动回中；不得用异时样本误报角度故障。
- 时间对齐后的两路差值超过 `2°` 仍立即锁存，并保留真实首因 `ANGLE_MISMATCH`，不再被泛化的显式停车覆盖。
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
| `Patrol_Vehicle_Chassis_Full.bin` | 190,384 B | `461685129DDBB990A4E6CAF22B523E443CF64E8A7BE490DCAE43ECD332F63910` |
| `Patrol_Vehicle_Chassis_App.bin` | 157,616 B | `9A2B3F7FB65E563D500E6999C1FC128F1AED5E2A306715ACB05AEA2B11A8D86D` |
| `BootState_A.bin` | 2,048 B | `2665E1ACB83A02081A11E63EA6D25BBF807F0BDFDC12C1FE9C2BC7A631378654` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`CD5B5F69`
- Full CRC32：`63E5C96E`
- State A record CRC32：`3D8C9064`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2 栈、Bench 栈、`git diff --check`、Full 拼接、版本标记、App 向量及真实 Boot State V3 解析/CRC 检查通过。全量主机回归为 49/57；8 项失败与修改前相同，为 1 项既有源码静态规则和 7 项历史证据链接失效。

尚未烧录或实车验证。现场应依次检查缓慢全幅左右转向、快速 `+450 → -450` 换向、跨中位是否先回中、双转角是否保持低于 `2°`，以及故障台是否记录真实 `ANGLE_MISMATCH`。
