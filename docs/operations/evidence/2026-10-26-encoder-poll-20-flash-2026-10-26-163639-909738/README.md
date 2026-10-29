# patrol-chassis-0.2.97 独立编码器20 ms轮询烧录包

本包将独立绝对编码器的正常运行轮询间隔从 `50 ms` 调整为 `20 ms`。配置寄存器首次读取仍保持 `500 ms`，不改变编码器协议、驱动器参数或两路转角 `2°`硬门禁。

## 烧录

- `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
- `BootState_A.bin` → `0x0803E800`
- `BootState_B.bin` → `0x0803F000`

也可以使用`Patrol_Vehicle_Chassis_App.bin` → `0x08008000`替代 Full，但仍须写入本包配套 State A/B。

不要整片擦除；保留：

- `0x0803E000..0x0803E7FF` 恢复页
- `0x0803F800..0x0803FFFF` Config V5 与故障日志页

## 校验

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `Patrol_Vehicle_Chassis_Full.bin` | 189,768 B | `8B0BBA0C5BAE4CA57C3B314A8A43222CC86A47099D1D7EB5C7B806E6019D4675` |
| `Patrol_Vehicle_Chassis_App.bin` | 157,000 B | `1ADBAFAB4013816B3AD484523F046E59E887C034960D71DA79A59EEC572EBA19` |
| `BootState_A.bin` | 2,048 B | `3AAE7A4F7086BD451EE9E609B3F2B791788D13EAA6186FF772280D020B3F4A22` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`3DD46E04`
- Full CRC32：`8C03F169`
- 初始 SP：`0x20010000`
- Reset 向量：`0x0800D44D`

## 验证边界

ARM Release 编译通过，RAM 使用 60 KiB / 64 KiB，Flash 使用 157,000 B；CAN2 和 Bench 调用链栈检查通过。完整主机测试保持 49/57，通过项包含本次相关的编码器、CAN、自动回中、中位标定、故障停车和停止调度路径；8 项失败与修改前相同，为 1 项既有源码静态规则和 7 项历史文档链接。尚未烧录验证实际 UART5 周期、编码器错误计数和实车转向。
