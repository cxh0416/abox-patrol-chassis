#include "steering_angle.h"

#include <limits.h>

uint8_t SteeringAngle_ToControllerCount(int32_t angle_mdeg, int32_t center,
    uint32_t counts_milli, int8_t direction, int32_t minimum, int32_t maximum, int32_t* target)
{
    int64_t result;
    if (!target || !counts_milli || (direction != 1 && direction != -1) || minimum > maximum) return 0U;
    /* int32 * uint32 fits int64, including the most negative int32. */
    result = (int64_t)angle_mdeg * counts_milli / 1000000LL;
    result = (int64_t)center + result * direction;
    if (result < INT32_MIN || result > INT32_MAX || result < minimum || result > maximum) return 0U;
    *target = (int32_t)result;
    return 1U;
}

static int32_t from_delta(int64_t delta, uint32_t counts_per_degree_milli, int8_t direction)
{
    int64_t angle;
    if (counts_per_degree_milli == 0U || (direction != 1 && direction != -1)) return 0;
    angle = delta * 1000000LL * direction / counts_per_degree_milli;
    if (angle > INT32_MAX) return INT32_MAX;
    if (angle < INT32_MIN) return INT32_MIN;
    return (int32_t)angle;
}

int32_t SteeringAngle_FromControllerCount(int32_t count, int32_t center,
                                          uint32_t counts_per_degree_milli,
                                          int8_t direction)
{
    return from_delta((int64_t)count - (int64_t)center, counts_per_degree_milli, direction);
}

int32_t SteeringAngle_FromAbsoluteCount(uint32_t count, uint32_t center,
                                        uint32_t counts_per_degree_milli,
                                        int8_t direction)
{
    return from_delta((int64_t)count - (int64_t)center, counts_per_degree_milli, direction);
}
