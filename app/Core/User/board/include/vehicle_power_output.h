#ifndef PATROL_CHASSIS_VEHICLE_POWER_OUTPUT_H
#define PATROL_CHASSIS_VEHICLE_POWER_OUTPUT_H

#include <stdint.h>

typedef uint8_t (*VehiclePowerOutputWriteFn)(uint8_t on);

// 绑定动力输出写回调并设置关闭的命令初值。有回调时尝试写关，当前初始化忽略其结果；不得把 RAM 初值当成接触器已断开的物理反馈。
void VehiclePowerOutput_Init(VehiclePowerOutputWriteFn write);
// 按编译许可提交动力输出开关命令。开启受 actuation 开关限制，只有写回调成功才更新命令缓存；返回成功仍不是继电器或接触器反馈。
uint8_t VehiclePowerOutput_Set(uint8_t on);
// 查询动力输出是否绑定了可用写回调。只表示软件端口存在，不能据此确认硬件供电或反馈线路正常。
uint8_t VehiclePowerOutput_IsAvailable(void);
// 读取最近一次成功提交的动力输出命令状态。这是命令缓存，不是实际电压、接触器吸合或电机停止状态测量。
uint8_t VehiclePowerOutput_IsOn(void);

#endif
