# patrol-chassis-0.2.104 显式停车与反馈过期误判修复烧录包

本包由当前工作树重新执行 ARM Release 构建并校验生成。

## 修改

- 显式停车释放会话后，仍由 `PREPARING/ACTIVE/STOPPING` 执行器继续完成有界零速和撤使能。
- 单次反馈过期只启动按轴锁存的受控停车，不再直接产生 `BenchDrive` fault 3。
- 停车责任按转向、行走两个轴分别保持，不会误停仅行走制动或自动回中的另一轴。
- 转向准备阶段可直接 shutdown，不因限位尚未对齐而误判故障。
- 节点离线、Bus-Off、驱动器故障、CAN 发送失败和停车序列超时仍进入故障锁存。

## 烧录

| 文件 | 地址 |
|---|---|
| `Patrol_Vehicle_Chassis_Full.bin` | `0x08000000` |
| `BootState_A.bin` | `0x0803E800` |
| `BootState_B.bin` | `0x0803F000` |

禁止整片擦除。保留 `0x0803E000..0x0803E7FF` 配置恢复页和 `0x0803F800..0x0803FFFF` Config V5/故障日志页。

## 文件

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `Patrol_Vehicle_Chassis_Full.bin` | 189,668 B | `1708BA5C0E66DC2868593645C96EAE40EA5905080085D9AB6AE970E78E3DDE61` |
| `BootState_A.bin` | 2,048 B | `CA6A02F33F62D8E78CF43D8E3C87A1D7E10012CA3D78B5D3B57CEB281F1F6D59` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- Full CRC32：`E2FBF29E`
- State A record CRC32：`CFE6111C`
- App CRC32：`00139201`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2/Bench 栈、Full 的冻结 Boot 前缀和 App 后缀、版本标记、向量、真实 Boot State V3 解析/CRC、State A 绑定及 State B 擦除检查通过。

全量产品回归为 48/56；剩余 8 项为既有静态身份规则 1 项和历史文档链接 7 项，`test_production_stop_arbitration` 已通过。本包尚未烧录或实车验证。
