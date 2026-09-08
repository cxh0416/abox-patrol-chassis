#include "steering_angle.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "vehicle_steering_profile.h"

void SteeringCoordinate_Reset(SteeringCoordinate* c) { memset(c, 0, sizeof(*c)); }
void SteeringCoordinate_Observe(SteeringCoordinate* c, uint8_t ready,
    int32_t driver, uint32_t absolute, uint32_t absolute_center, uint32_t sample_ms, uint32_t now)
{
    int64_t offset, center;
    if (c->valid) return; /* Never hide a disagreement by re-zeroing while running. */
    if (!ready) { c->tracking = 0; return; }
    if (!c->tracking || llabs((int64_t)driver - c->anchor_driver) > 27 ||
        llabs((int64_t)absolute - c->anchor_absolute) > 1) {
        c->tracking = 1; c->samples = 1; c->quiet_since = now;
        c->anchor_driver = driver; c->anchor_absolute = absolute; c->last_sample_ms = sample_ms;
        return;
    }
    if (sample_ms == c->last_sample_ms) return;
    c->last_sample_ms = sample_ms;
    if (c->samples < 255) c->samples++;
    if (c->samples < 3 || now - c->quiet_since < 300U) return;
    offset = ((int64_t)absolute - absolute_center) * VEHICLE_STEERING_COUNTS_MILLI *
        VEHICLE_ABSOLUTE_DIRECTION * VEHICLE_STEERING_DIRECTION / VEHICLE_ABSOLUTE_COUNTS_MILLI;
    center = (int64_t)driver - offset;
    if (center < INT32_MIN || center > INT32_MAX) return;
    c->runtime_center = (int32_t)center;
    c->valid = 1;
}

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
