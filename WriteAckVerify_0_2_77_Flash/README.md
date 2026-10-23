# 0.2.77 烧录文件与地址说明

产品：`patrol_vehicle_chassis`。固件版本：`patrol-chassis-0.2.77`。

本文件夹是操作者指定的当前实车烧录文件目录。下面两种烧录方式选择一种即可，推荐使用方式一。

## 方式一：完整烧录（推荐）

在烧录软件中选择 **BIN / 二进制文件**，分别设置以下起始地址：

| 顺序 | 要烧录的文件 | 起始地址 | 文件大小 | 作用 |
| --- | --- | --- | --- | --- |
| 1 | `Patrol_Vehicle_Chassis_Full.bin` | `0x08000000` | 187,096 字节 | 包含 Boot 和 App |
| 2 | `BootState_A.bin` | `0x0803E800` | 2,048 字节 | 与本版 App 配套的启动状态 A |
| 3 | `BootState_B.bin` | `0x0803F000` | 2,048 字节 | 与本版 App 配套的启动状态 B |

**三个文件全部写入并校验成功后，再复位或重新上电。**

`Full.bin` 已包含 Boot 和 App，采用本方式时无需再单独烧录 `ABox_Boot.bin` 或 `Patrol_Vehicle_Chassis_App.bin`。Full 文件不包含启动状态页和逐车配置，不能只烧 Full 而漏掉本包的 State A/B。

## 方式二：保留已有 Boot，仅更新 App

仅在开发板已有与本包 `ABox_Boot.bin` 匹配的 Boot 时使用：

| 顺序 | 要烧录的文件 | 起始地址 |
| --- | --- | --- |
| 1 | `Patrol_Vehicle_Chassis_App.bin` | `0x08008000` |
| 2 | `BootState_A.bin` | `0x0803E800` |
| 3 | `BootState_B.bin` | `0x0803F000` |

同样需要全部写入、校验成功后再复位。单独的 `ABox_Boot.bin` 烧录地址为 `0x08000000`，大小为 32,768 字节；不确定现有 Boot 是否匹配时，使用方式一。

## 烧录操作与保留区域

1. 停稳车辆，关闭电机动力电源，仅保留烧录所需的主控供电与调试连接。
2. 按所选方式加载文件，核对文件名和起始地址。
3. 选择按所需页/扇区擦除，**不要整片擦除（Mass Erase / Erase All）**，避免丢失逐车配置。若软件或工具只能整片擦除，应先按逐车维护流程备份配置，再另行恢复；本文件夹不提供通用逐车配置文件。
4. 将本包配套的三个文件全部烧录，并执行读回校验；不要混用其他版本的 State A/B。
5. 完成后复位主控，先检查启动与遥控状态，再恢复动力供电并进行低速功能检查。

必须保留的 Flash 页面：

| 区域 | 地址范围 | 要求 |
| --- | --- | --- |
| 配置恢复页 | `0x0803E000`～`0x0803E7FF` | 保留，不写入上述固件文件 |
| Config V5 逐车配置页 | `0x0803F800`～`0x0803FFFF` | 保留，不整片擦除或套用其他车辆配置 |

本包 State A/B 是离线生成的 NORMAL 启动记录，不代表已经建立有效的 stable UFS 镜像。

## 文件核对

本目录五个 BIN 已与原始归档逐字节核对一致；Full 的内容等于本包 Boot 加 App。

| 文件 | SHA256 |
| --- | --- |
| `Patrol_Vehicle_Chassis_Full.bin` | `ab7944e467084d3eaf0a6ec3b64d9423eb98117950de20aac09c4d0acc2d9212` |
| `Patrol_Vehicle_Chassis_App.bin` | `ced4fd110f4c02b1108a99cc3c2c2eb0d8d948e07232aaa04441741a219ec020` |
| `BootState_A.bin` | `9e7023be4232d4a281ee7567b62abd2f3c1e957bcc540a6564a13eaeb417abb8` |
| `BootState_B.bin` | `35bfac3c79e2bdeb539d1deaae5b097e18ad27aec6b1bc3988f89deeb08b17c0` |
| `ABox_Boot.bin` | `981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63` |

说明文档、TXT 和 JSON 清单均无需写入开发板。`FLASH_ADDRESSES.txt` 与原始清单保留生成时的候选状态记录；当前目录位置、操作者确认的 0.2.77 实车测试结果以本说明及 [当前项目状态](../docs/CURRENT.md) 为入口。指定本目录作为实车烧录文件不改写原构建清单，也不替换 `dist/release.manifest.json` 所标识的正式发布包。
