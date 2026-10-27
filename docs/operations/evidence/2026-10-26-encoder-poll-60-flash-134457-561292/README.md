# patrol-chassis-0.2.92 绝对编码器 60 ms 轮询烧录包

本包只修改正常运行阶段的绝对编码器轮询延时：

```c
vTaskDelay(pdMS_TO_TICKS(read_config ? 500U : 60U));
```

配置读取阶段仍为 500 ms。构建配置为 Release、`active_car=0`，转向和行走执行器启用，启动许可关闭。

## 烧录

- `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
- `BootState_A.bin` → `0x0803E800`
- `BootState_B.bin` → `0x0803F000`

也可以使用 `Patrol_Vehicle_Chassis_App.bin` → `0x08008000` 替代 Full，但仍须写入配套 State A/B。

不要整片擦除；保留 `0x0803E000..0x0803E7FF` 恢复页和 `0x0803F800..0x0803FFFF` Config V5 页。

## 验证边界

ARM Release 编译和 Bench 栈检查通过；Full/Boot/App 拼接、版本标记、向量地址及 Boot State A 记录已校验。尚未连接开发板烧录，也未完成实车转向和通信负载验收。
