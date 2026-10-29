# patrol-chassis-0.2.95 转向PDO实时反馈烧录包

本包将转向实时状态和位置切换为PDO，并增加每次启动执行一次的引导序列：

```text
0x181：状态字
0x281：位置反馈
```

启动后先读取一次`0x6041`，再以150 ms间隔读取三次`0x6064`，用于建立稳定运行坐标。引导完成后`0x6041/0x6064`永久停用，运行期间不会周期读取或回退SDO。
模式、限位、速度、控制字、目标位置、行走轮速、厂商故障和安全停机继续使用SDO。
绝对编码器UART轮询仍保持50 ms。构建配置为Release、`active_car=0`，转向和行走执行器启用，启动许可关闭。

## 烧录

- `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
- `BootState_A.bin` → `0x0803E800`
- `BootState_B.bin` → `0x0803F000`

也可以使用 `Patrol_Vehicle_Chassis_App.bin` → `0x08008000` 替代 Full，但仍须写入配套 State A/B。

不要整片擦除；保留 `0x0803E000..0x0803E7FF` 恢复页和 `0x0803F800..0x0803FFFF` Config V5 页。

## 验证边界

ARM Release编译和Bench栈检查通过；Full/Boot/App拼接、版本标记、向量地址及Boot State A记录已校验。尚未连接开发板烧录，也未完成实车转向和通信负载验收。
