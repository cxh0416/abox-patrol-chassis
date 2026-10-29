# Historical PC raw-SDO gateway debugging

Archived 2026-10-20 after replacing the normal keyboard path with MCU CAN2 commands. The following is a historical workflow, not current operating instructions. Field evidence and hashes referenced below remain unchanged.

## CAN2 线下脚本选车

本机线下工具为 `E:\1_work\ChassisCar_test\chassis_offline_remote.py`，通过 CANalyst-II 通道 2 接主控 CAN2；该路径的主控固件承担 CAN 透传。正常控制必须显式指定 `--car 1..4`，不从固件 `PATROL_ACTIVE_CAR` 获取车辆编号，也不自动识别所连接车辆。

```powershell
python E:\1_work\ChassisCar_test\chassis_offline_remote.py --car 2 --channel 1 --dll "E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll"
```

第一台车将参数改为 `--car 1`。四台车共用同一脚本，第三、第四台使用 `--car 3` / `--car 4`。电脑端档案为脚本同目录的 `car_profiles.json`，不是 Flash Config V5；当前 CAR1/CAR2 分别记录驱动器机械中位 `-83815`/`-1463`，编码器中位均为 0。CAR3/CAR4 中位为 `null`，拒绝控制；完成逐车标定后登记本车实测值。仅修改电脑档案无需重新编译透传固件；若使用固件逐车匹配门禁，须另行同步 `patrol_config.c` 的车辆表及本车 Flash Config V5，并构建核验相应固件。档案缺失、损坏或无效时在打开 CAN 前拒绝正常控制。只读排查可不指定车辆，在原命令末尾追加 `--diagnose-steering`；档案不可用也不阻止该只读分支。

未提供数据时可离线查看档案位置、字段含义和待填提示，不加载 DLL、不打开 CAN：

```powershell
python E:\1_work\ChassisCar_test\chassis_offline_remote.py --car 3 --show-car-profile
python E:\1_work\ChassisCar_test\chassis_offline_remote.py --car 4 --show-car-profile
```

JSON 中 `car=3` / `car=4` 各自的 `steering_center_count` 是本车经确认机械摆正后，Node 1 `0x6064` 的有符号32位原始计数，单位 count；后续用各自实测值替换 `null`，保留正负号。`todo` 中分别以 `TODO_CAR3_STEERING_CENTER` / `TODO_CAR4_STEERING_CENTER` 标明缺项，不参与控制；不能把提示字符串或猜测的 0 写入中位字段。`absolute_center_count=0` 是独立编码器完成机械中位置零后的产品要求，不是该车已标定的证明。`0x201A` 下限和 `0x2019` 上限与 `0x6064` 使用相同单位，当前从驱动器实时读取，不在电脑档案中填写；不能把其他车或历史限位直接套用于本车。

选车后，手动角度目标以档案中位作为参考；启动保持目标仍为本次实测 `6064`，不触发回中。启动验证已有 `201A < 2019`，且启动位置与档案中位都处于范围内。零限位仍拒绝正常启动，不自动恢复或保存软限位；独立参数维护入口见下文。正常退出或异常清理只尝试左右零速和双节点撤使能，不主动回中；W/S、空格和松键后的 0° 请求仍属于手动控制流程中的中位目标。默认目标已收紧为 50 rpm、5°，参数上限保持 500 rpm、30°。

当前选车功能未接入独立编码器及本供电周期坐标重建；档案选择和限位包含关系不能作为当前运动许可。自动运行坐标与限位初始化、上电回中和完整反馈门禁仍待接入：先确认行走停止、标定及双路反馈有效，回中完成并核验两路实测角后才允许行走，超时或故障保持运动禁止。电脑端档案不能替代下面的逐车标定与复读验收。

线下脚本当前提供独立 `--restore-steering-limits --confirmed-runtime-center <本次已确认机械中位计数>` 维护动作，须同时选择已登记车辆；不从历史档案猜测本次运行中位。先在现场核对本车身份、本次上电坐标、架空及硬件急停，确保没有其他控制源。该动作分别发送左右零速和双节点撤使能，要求三对新鲜轮速 ≤20 rpm、双方已撤使能且无厂商故障；按本产品比例计算运行中位 ±30.5° RAM 限位，每次写入前核验撤使能及位置静止、写后复读，最后复读整对。不写定位目标、使能、EEP 或车辆档案，也不进入键盘循环。只允许原有效范围或已知零/零范围；部分写入或结果不确定时不自动重试/回滚，保持停机并复读。断电后重新核验。模拟命令及详细失败处理见工具目录 README 第 7 节。

维护成功不代表允许行走。当前键盘目标仍使用档案中位，维护传入的运行中位不会自动跨进程传递；两者不同时必须先解决运行坐标，不能直接继续控制。脚本已补充按键不变时的驱动状态、位置、轮速及 `2000/2001` 故障轮询，缺失或故障结束控制并尝试独立零速/撤使能；W→松键/空格→S 也必须重新确认零速，已松开的反向请求不延迟执行。但独立编码器、MCU OTA/Bus-Off 状态及硬件侧租约停车保护尚未接入，电脑进程停止不能保证驱动器停车；不能以这些主机检查替代正式运动安全链路或整车验收。

