#include "vehicle_power_output.h"

#ifndef PATROL_CHASSIS_ACTUATION_ENABLED
#define PATROL_CHASSIS_ACTUATION_ENABLED 0
#endif

static VehiclePowerOutputWriteFn g_write;
static uint8_t g_commanded_on;

// 绑定动力输出写回调并设置关闭的命令初值。有回调时尝试写关，当前初始化忽略其结果；不得把 RAM 初值当成接触器已断开的物理反馈。
void VehiclePowerOutput_Init(VehiclePowerOutputWriteFn write)
{
    g_write = write;
    g_commanded_on = 0U;
    if (g_write != 0) (void)g_write(0U);
}

// 按编译许可提交动力输出开关命令。开启受 actuation 开关限制，只有写回调成功才更新命令缓存；返回成功仍不是继电器或接触器反馈。
uint8_t VehiclePowerOutput_Set(uint8_t on)
{
    uint8_t target = on ? 1U : 0U;
    if (target && !PATROL_CHASSIS_ACTUATION_ENABLED) return 0U;
    if (g_write == 0 || !g_write(target)) return 0U;
    g_commanded_on = target;
    return 1U;
}

// 查询动力输出是否绑定了可用写回调。只表示软件端口存在，不能据此确认硬件供电或反馈线路正常。
uint8_t VehiclePowerOutput_IsAvailable(void)
{
    return g_write != 0 ? 1U : 0U;
}

// 读取最近一次成功提交的动力输出命令状态。这是命令缓存，不是实际电压、接触器吸合或电机停止状态测量。
uint8_t VehiclePowerOutput_IsOn(void)
{
    return g_commanded_on;
}
