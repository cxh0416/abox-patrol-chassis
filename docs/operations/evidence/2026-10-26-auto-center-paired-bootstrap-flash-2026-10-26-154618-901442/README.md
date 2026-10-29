# patrol-chassis-0.2.96 自动回中启动引导修复包

本包修复上电后转速、位置和编码器反馈可用，但转向运行坐标未建立，导致自动回中不启动的问题。

启动引导改为有界的状态字/位置成对采样：

```text
0x6041 状态字
0x6064 位置
间隔 100 ms
最多 6 组，约 0.6 s
```

位置稳定窗口只使用`0x6064`自身的新样本时间推进，不再被其他反馈时间戳重复触发。六组样本完成，或任一引导事务失败后，Node 1 的`0x6041/0x6064`引导读取永久停止；运行期实时状态和位置仍只使用`0x181/0x281` PDO。

## 烧录

- `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
- `BootState_A.bin` → `0x0803E800`
- `BootState_B.bin` → `0x0803F000`

也可以使用`Patrol_Vehicle_Chassis_App.bin` → `0x08008000`替代 Full，但仍须写入本包配套的 State A/B。

不要整片擦除；保留：

- `0x0803E000..0x0803E7FF` 恢复页
- `0x0803F800..0x0803FFFF` Config V5 与故障日志页

## 校验

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `Patrol_Vehicle_Chassis_Full.bin` | 189,776 B | `13B9907F5D7AC50E1A705FA097CA4AA24EE559C08ECD5FBED7D7A5C5C16E90FF` |
| `Patrol_Vehicle_Chassis_App.bin` | 157,008 B | `92DE4329D9951A86D617EB842EE52DF18E3F73C3ADA19E3512BEF332181A25D3` |
| `BootState_A.bin` | 2,048 B | `20D379D3C866A51D218E068B1963A7DE1EE514CA7CDD1A286F2A4DA39C6A77E0` |
| `BootState_B.bin` | 2,048 B | `D0FF1B294B5288D1AE1421EADF5B2D38A8752B76D472FF30BED9028E25B1C5B8` |

- App CRC32：`87E52F5F`
- Full CRC32：`F71791D3`
- 初始 SP：`0x20010000`
- Reset 向量：`0x0800D44D`

## 验证边界

ARM Release 编译通过，RAM 使用 60 KiB / 64 KiB，Flash 使用 157,008 B；CAN2 和 Bench 调用链栈检查通过。完整主机测试中产品相关用例通过，仍保留既有的 1 项源码静态规则失败和 7 项历史文档链接失败。本包尚未完成实车自动回中和 PDO 启动验收。
