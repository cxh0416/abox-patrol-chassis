# 写后核验 60/80/200 ms 烧录包

版本：`patrol-chassis-0.2.85`

本包包含：

- `Patrol_Vehicle_Chassis_Full.bin`：冻结 Boot + App
- `BootState_A.bin`：与本 App 匹配的 Boot State V3
- `BootState_B.bin`：擦除页
- `Patrol_Vehicle_Chassis_App.bin`：可在已核对 Boot 时单独烧写
- `ABox_Boot.bin`：冻结 Boot，供组成和核对 Full 使用

烧录：

- `Patrol_Vehicle_Chassis_Full.bin -> 0x08000000`
- `BootState_A.bin -> 0x0803E800`
- `BootState_B.bin -> 0x0803F000`

已有匹配冻结 Boot 时，可改为：

- `Patrol_Vehicle_Chassis_App.bin -> 0x08008000`
- 仍必须同时烧写本包的 `BootState_A.bin` 和 `BootState_B.bin`

不要整片擦除。保留 `0x0803E000..0x0803E7FF` 配置恢复页和
`0x0803F800..0x0803FFFF` Config V5/故障日志页。

校验：

- App 大小 `154712` 字节，CRC32 `0x95355B90`
- Full 大小 `187480` 字节；偏移 `0x8000` 处向量与独立 App 一致
- State A 大小 `2048` 字节，绑定 App `154712 / 0x95355B90 / patrol-chassis-0.2.85`
- State B 大小 `2048` 字节，内容为擦除态 `0xFF`
- 实际 Boot State V3 解析器、匹配启动、损坏 State/App 拒绝启动检查通过
- ARM Release、栈、CAN2 栈和 Flash/RAM 布局检查通过
- 写后核验与故障停机专项回归通过

完整哈希见 `sha256.json` 和 `flash.manifest.json`。本包尚未烧录，尚未做实车运动验收。
