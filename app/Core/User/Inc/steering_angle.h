#ifndef PATROL_CHASSIS_STEERING_ANGLE_H
#define PATROL_CHASSIS_STEERING_ANGLE_H

#include <stdint.h>

typedef struct {
    uint8_t valid, tracking, samples;
    uint32_t quiet_since, last_sample_ms, anchor_absolute;
    int32_t anchor_driver, runtime_center;
} SteeringCoordinate;
void SteeringCoordinate_Reset(SteeringCoordinate* coordinate);
void SteeringCoordinate_Observe(SteeringCoordinate* coordinate, uint8_t ready,
    int32_t driver, uint32_t absolute, uint32_t absolute_center, uint32_t sample_ms, uint32_t now);

uint8_t SteeringAngle_ToControllerCount(int32_t angle_mdeg, int32_t center,
    uint32_t counts_milli, int8_t direction, int32_t minimum, int32_t maximum, int32_t* target);

int32_t SteeringAngle_FromControllerCount(int32_t count, int32_t center,
                                          uint32_t counts_per_degree_milli,
                                          int8_t direction);
int32_t SteeringAngle_FromAbsoluteCount(uint32_t count, uint32_t center,
                                        uint32_t counts_per_degree_milli,
                                        int8_t direction);
uint8_t SteeringAngle_AbsoluteWithinLimit(uint32_t count, uint32_t center,
    uint32_t counts_per_degree_milli, int8_t direction, int32_t limit_mdeg,
    int32_t* angle_mdeg);
uint8_t SteeringAngle_AccumulateSingleTurn(uint32_t previous, uint32_t current,
    uint32_t single_turn_counts, uint32_t max_step_counts, int32_t* accumulated);

#endif
