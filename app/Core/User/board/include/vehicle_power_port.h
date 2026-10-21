#ifndef PATROL_CHASSIS_VEHICLE_POWER_PORT_H
#define PATROL_CHASSIS_VEHICLE_POWER_PORT_H

#include <stdint.h>
#include "abox_board_io.h"

// 按编译许可初始化公共板级 IO 并绑定动力输出。只有明确启用的 OUT1 可写，初始化失败或输出禁用时绑定空回调，保持不可开启状态。
void VehiclePowerPort_Init(void);
// 按当前毫秒时间轮询板级 IO。由公共服务维护输入等状态，不在此授予底盘运动使能。
void VehiclePowerPort_Poll(uint32_t now_ms);
// 取得公共板级 IO 输入快照。返回端口结果枚举，调用者必须区分有效采样与未配置反馈，不能凭空推导接触器状态。
ABoxBoardIoResult VehiclePowerPort_InputSnapshot(ABoxBoardIoInputs *inputs);

#endif
