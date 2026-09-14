#ifndef PATROL_CHASSIS_VEHICLE_POWER_OUTPUT_H
#define PATROL_CHASSIS_VEHICLE_POWER_OUTPUT_H

#include <stdint.h>

typedef uint8_t (*VehiclePowerOutputWriteFn)(uint8_t on);

void VehiclePowerOutput_Init(VehiclePowerOutputWriteFn write);
uint8_t VehiclePowerOutput_Set(uint8_t on);
uint8_t VehiclePowerOutput_IsAvailable(void);
uint8_t VehiclePowerOutput_IsOn(void);

#endif
