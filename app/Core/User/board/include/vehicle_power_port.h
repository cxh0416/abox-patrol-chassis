#ifndef PATROL_CHASSIS_VEHICLE_POWER_PORT_H
#define PATROL_CHASSIS_VEHICLE_POWER_PORT_H

#include <stdint.h>
#include "abox_board_io.h"

void VehiclePowerPort_Init(void);
void VehiclePowerPort_Poll(uint32_t now_ms);
ABoxBoardIoResult VehiclePowerPort_InputSnapshot(ABoxBoardIoInputs *inputs);

#endif
