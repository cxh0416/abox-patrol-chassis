# patrol-chassis-0.2.105 实际转角内轮差速烧录包

本包由当前工作树重新执行 ARM Release 构建并校验生成。

## 修改

- 使用转向电机反馈换算的实际车轮角度选择行走内轮，而不依赖摇杆目标角。
- `±1°` 内两轮等速；小角度内轮为外轮的 `90%`，大角度为 `65%`。
- 大角度档在 `11°` 进入、`9°` 退出，中间保持前一档位。
- 前进和倒车均按相同比例缩放；左转减左轮，右转减右轮。
- 左右目标对发送期间保持冻结，转角变化不会替换未完成的右轮写入。
- 左右轮丢失 ACK 时分别核验各自冻结目标；双零速、三次停车反馈、6 秒换向期限及驱动器故障门禁保持不变。
- 烧录变体使用 `PATROL_ACTIVE_CAR=0`，不启用逐车精确中位档比较。

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
| `Patrol_Vehicle_Chassis_Full.bin` | 190,028 B | `9C1B5F33D362BC5C28796AB029B439C6443B51A6CB7FA2686A7A2BEE541B9697` |
| `BootState_A.bin` | 2,048 B | `33DC1F880C3A636B98D01D094FF97DAD9B9ABB55567D37008936A356F47DE2C3` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- Full CRC32：`4A496685`
- State A record CRC32：`CBDED0C2`
- App CRC32：`39E5B6E5`
- 初始 SP：`0x20010000`
- Reset 向量：`0x800d44d`

## 验证边界

ARM Release、CAN2/Bench 栈、Full 的冻结 Boot 前缀和 App 后缀、版本标记、向量、真实 Boot State V3 解析/CRC、State A 绑定及 State B 擦除检查通过。

全量产品回归为 48/56；剩余 8 项为既有静态身份规则 1 项和历史文档链接 7 项，`test_production_stop_arbitration` 已通过。本包尚未烧录或实车验证。
