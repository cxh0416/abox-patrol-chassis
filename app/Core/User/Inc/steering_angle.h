#ifndef PATROL_CHASSIS_STEERING_ANGLE_H
#define PATROL_CHASSIS_STEERING_ANGLE_H

#include <stdint.h>

uint8_t SteeringAngle_ToControllerCount(int32_t angle_mdeg, int32_t center,
    uint32_t counts_milli, int8_t direction, int32_t minimum, int32_t maximum, int32_t* target);

int32_t SteeringAngle_FromControllerCount(int32_t count, int32_t center,
                                          uint32_t counts_per_degree_milli,
                                          int8_t direction);
int32_t SteeringAngle_FromAbsoluteCount(uint32_t count, uint32_t center,
                                        uint32_t counts_per_degree_milli,
                                        int8_t direction);

#endif
