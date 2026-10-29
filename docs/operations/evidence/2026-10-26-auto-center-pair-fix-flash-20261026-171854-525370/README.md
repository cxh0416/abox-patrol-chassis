# patrol-chassis-0.2.99 自动回中配对门禁回归修复烧录包

本包修复 `0.2.98` 在自动回中启动后，两路角度样本短暂失去 `30 ms` 时间配对时，把通用安全门禁判为失败并进入紧急切电的问题。

## 修改

- `angle_pair_valid` 只约束新运动请求与自动回中的首次启动；不再作为整机通用安全门禁。
- 自动回中已启动后，配对短暂抖动不会中断回中；若原始反馈过期，仍由既有 250 ms 时效和受控停机路径处理。
- 未形成有效角度配对时仍拒绝新的遥控/CAN 运动，并禁止新自动回中请求。
- 实体遥控转向目标限制为 `12000 mdeg/s`，约 `12°/s`。
- 目标跨过中位且变化达到 `5000 mdeg` 时，先回中；两路角度均在 `±1000 mdeg` 且连续 3 个有效帧稳定后，再转向新目标。
- 只有驱动器位置与独立编码器样本时间差不超过 `30 ms` 时，才比较两路角度差值。
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
| `Patrol_Vehicle_Chassis_Full.bin` | 190,400 B | `C5284D973EDCB02995057B73D9E68BFF552BBEBC60FDC340AF21B92055537FCD` |
| `Patrol_Vehicle_Chassis_App.bin` | 157,632 B | `B430A729D61A1A87A9EC62962DF15DDB2C37EBE8AAD25922AAA943D025B367C4` |
| `BootState_A.bin` | 2,048 B | `3E160EA20F280D41CA86DEC1466B5D965538978372CE3B8F896943A3CFF7F93E` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`B198BCC5`
- Full CRC32：`6EB26332`
- State A record CRC32：`2BA3CAC0`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2 栈、Bench 栈、`git diff --check`、Full 拼接、版本标记、App 向量及真实 Boot State V3 解析/CRC 检查通过。全量主机回归为 49/57；8 项失败与修改前相同，为 1 项既有源码静态规则和 7 项历史证据链接失效。

尚未烧录或实车验证。现场应首先确认断电重启后转向轮能自动回中且不锁存执行故障，再依次检查缓慢全幅左右转向、快速 `+450 → -450` 换向、跨中位是否先回中、双转角是否保持低于 `2°`，以及真实 `ANGLE_MISMATCH` 的故障记录。
