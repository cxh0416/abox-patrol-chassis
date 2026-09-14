# 30度边界触发越界保护

0.2.29快照序号2：source1/reason14=编码器范围。encoderRaw2437相对0跨圈约-30.027度，driverRaw-165039相对中心-83815约-29.963度；状态字8759=0x2237、4151=0x1037均未置Fault位，A/B详细码旧样本均0。后续编码器2436约-30.045度。

reset_motion_fault d9e3b475-001a-4372-a69a-ce9d48b268d4成功，之后用户power_on；当前faultLatched=false但rangeValid=false、coordinateValid=false，因此普通远控及自动回中拒绝。不是证明机械已撞限位，也不是驱动器内部保护已发生。本轮仅只读诊断，未运动、改中位或放宽保护。

out/logs/turn30-fault.log → turn30-fault.log SHA256 ca64374e42193d9fdae9488ca2b61e99583112c2b94a80adb9641f9cdad0dff1

out/logs/turn30-current.log → turn30-current.log SHA256 95ee43e6dfaa580e7b620d040757001e81b08a04b576fec7675e35a310979e06
