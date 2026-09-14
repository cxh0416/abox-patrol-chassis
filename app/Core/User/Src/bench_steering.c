#include "bench_steering.h"
#include "steering_angle.h"
#include "vehicle_steering_profile.h"
#include <limits.h>
#include <string.h>

/* No write retries: a trigger whose acknowledgement was lost must never replay. */
#define TRANSACTION_MS 100U
#define PREPARE_MS 5000U
#define STOP_MS 200U
#define BENCH_STEERING_ENVELOPE_MDEG VEHICLE_MAX_STEERING_MDEG

/* Values retain existing diagnostic codes and ordered transitions. */
typedef enum
{
    STEER_STEP_START_NODE = 0,
    STEER_STEP_READ_MINIMUM = 1,
    STEER_STEP_READ_MAXIMUM = 2,
    STEER_STEP_WRITE_MODE = 3,
    STEER_STEP_READ_MODE = 4,
    STEER_STEP_WRITE_MAX_SPEED = 5,
    STEER_STEP_READ_MAX_SPEED = 6,
    STEER_STEP_WRITE_PROFILE_SPEED = 7,
    STEER_STEP_READ_PROFILE_SPEED = 8,
    STEER_STEP_HOLD_POSITION = 9,
    STEER_STEP_SHUTDOWN = 10,
    STEER_STEP_READ_READY = 11,
    STEER_STEP_SWITCH_ON = 12,
    STEER_STEP_READ_SWITCHED = 13,
    STEER_STEP_ENABLE = 14,
    STEER_STEP_READ_ENABLED = 15,
    STEER_STEP_ACTIVE = 16,
    STEER_STEP_TARGET = 17,
    STEER_STEP_TRIGGER = 18,
    STEER_STEP_CLEAR_TRIGGER = 19,
    STEER_STEP_TARGET_COMPLETE = 20,
    STEER_STEP_STOP = 21,
    STEER_STEP_READ_DISABLED = 22,
    STEER_STEP_STOP_COMPLETE = 23,
    STEER_STEP_WRITE_FIRST_LIMIT = 24,
    STEER_STEP_READ_FIRST_LIMIT = 25,
    STEER_STEP_WRITE_SECOND_LIMIT = 26,
    STEER_STEP_READ_SECOND_LIMIT = 27,
    STEER_STEP_LIMITS_COMPLETE = 28,
} BenchSteeringStep;

static uint8_t valid_limit(uint16_t limit)
{
    return (limit == 1000 || limit == 3000 || limit == 5000 || limit == 20000 || limit == 30000) ? 1U : 0U;
}

static void fail(BenchSteering *b, uint32_t reason)
{
    /* Cross-executor emergency stopping must not replace the first cause. */
    if (b->phase == BENCH_FAULT)
        return;
    b->pending = 0;
    b->fault = reason;
    b->cut_power = 1;
    b->phase = BENCH_FAULT;
}
void BenchSteering_Init(BenchSteering *b)
{
    memset(b, 0, sizeof(*b));
}
uint8_t BenchSteering_Start(BenchSteering *b, int32_t actual, int32_t center, int32_t calibration_center,
                            uint16_t limit, uint32_t now)
{
    uint8_t aligned = b->limits_aligned;
    int32_t minimum = b->cached_minimum, maximum = b->cached_maximum;
    if (b->phase != BENCH_IDLE || !valid_limit(limit))
        return 0;
    BenchSteering_Init(b);
    b->limits_aligned = aligned;
    b->cached_minimum = minimum;
    b->cached_maximum = maximum;
    b->calibration_center = calibration_center;
    b->phase = BENCH_PREPARING;
    b->started_ms = now;
    b->center = center;
    b->hold = actual;
    b->minimum = INT32_MIN;
    b->maximum = INT32_MAX;
    b->limit_mdeg = limit;
    b->applied = INT32_MIN;
    return 1;
}
uint8_t BenchSteering_StartCommissioning(BenchSteering *b, int32_t actual, int32_t center, uint32_t limit,
                                         uint32_t now)
{
    if (limit == 0U || limit > INT32_MAX || b->phase != BENCH_IDLE)
        return 0U;
    BenchSteering_Init(b);
    b->preserve_limits = 1U;
    b->calibration_center = center;
    b->phase = BENCH_PREPARING;
    b->started_ms = now;
    b->center = center;
    b->hold = actual;
    b->minimum = INT32_MIN;
    b->maximum = INT32_MAX;
    b->limit_mdeg = limit > UINT16_MAX ? UINT16_MAX : (uint16_t)limit;
    b->applied = INT32_MIN;
    return 1U;
}
void BenchSteering_Stop(BenchSteering *b, uint32_t now, uint8_t cut_power)
{
    if (cut_power)
    {
        fail(b, 1U);
        return;
    }
    if (b->phase == BENCH_IDLE || b->phase == BENCH_FAULT || b->phase == BENCH_STOPPING)
        return;
    /* A partially rewritten limit pair must be restored by a power cycle. */
    if (b->phase == BENCH_PREPARING && !b->limits_aligned)
    {
        fail(b, 11U);
        return;
    }
    b->pending = 0;
    b->phase = BENCH_STOPPING;
    b->step = STEER_STEP_STOP;
    b->stop_ms = now;
}
void BenchSteering_TxFailed(BenchSteering *b)
{
    fail(b, 2U);
}
void BenchSteering_Tick(BenchSteering *b, uint32_t now, uint8_t requested, uint8_t safe, int32_t angle)
{
    if (b->phase == BENCH_IDLE || b->phase == BENCH_FAULT)
        return;
    if (!safe)
    {
        BenchSteering_Stop(b, now, 1U);
        return;
    }
    if (!requested)
        BenchSteering_Stop(b, now, 0U);
    if (b->phase == BENCH_STOPPING)
    {
        if (now - b->stop_ms >= STOP_MS)
            fail(b, 3U);
    }
    else
    {
        if (angle < -(int32_t)b->limit_mdeg || angle > b->limit_mdeg ||
            !SteeringAngle_ToControllerCount(angle, b->center, VEHICLE_STEERING_COUNTS_MILLI,
                                             VEHICLE_STEERING_DIRECTION, b->minimum, b->maximum, &b->desired))
        {
            fail(b, 4U);
            return;
        }
        if (b->phase == BENCH_PREPARING && now - b->started_ms >= PREPARE_MS)
        {
            fail(b, 5U);
            return;
        }
        if (b->phase == BENCH_ACTIVE && b->step == STEER_STEP_ACTIVE && b->desired != b->applied)
            b->step = STEER_STEP_TARGET;
    }
    if (b->pending && now - b->sent_ms >= TRANSACTION_MS)
        fail(b, 6U);
}

uint8_t BenchSteering_Take(BenchSteering *b, uint32_t now, PatrolCanFrame *f)
{
    int32_t value = 0;
    if (b->pending || b->phase == BENCH_IDLE || b->phase == BENCH_FAULT || b->step == STEER_STEP_ACTIVE)
        return 0;
    if (b->step == STEER_STEP_START_NODE || b->step == STEER_STEP_STOP_COMPLETE)
    {
        memset(f, 0, sizeof(*f));
        f->dlc = 2;
        f->data[0] = b->step == STEER_STEP_START_NODE ? 1 : 0x80;
        f->data[1] = 1;
        if (b->step == STEER_STEP_STOP_COMPLETE)
            b->phase = BENCH_IDLE;
        else
            b->step++;
        return 1;
    }
    b->read = 0;
    b->width = 2;
    switch (b->step)
    {
    case STEER_STEP_READ_MINIMUM:
        b->index = 0x201A;
        b->read = 1;
        b->width = 4;
        break;
    case STEER_STEP_READ_MAXIMUM:
        b->index = 0x2019;
        b->read = 1;
        b->width = 4;
        break;
    case STEER_STEP_WRITE_MODE:
        b->index = 0x6060;
        b->width = 1;
        value = 1;
        break;
    case STEER_STEP_READ_MODE:
        b->index = 0x6061;
        b->read = 1;
        b->width = 1;
        b->expected = 1;
        break;
    case STEER_STEP_WRITE_MAX_SPEED:
    case STEER_STEP_WRITE_PROFILE_SPEED:
        b->index = b->step == STEER_STEP_WRITE_MAX_SPEED ? 0x6080 : 0x6081;
        b->width = 4;
        value = VEHICLE_BENCH_MOTOR_RPM;
        break;
    case STEER_STEP_READ_MAX_SPEED:
    case STEER_STEP_READ_PROFILE_SPEED:
        b->index = b->step == STEER_STEP_READ_MAX_SPEED ? 0x6080 : 0x6081;
        b->width = 4;
        b->read = 1;
        b->expected = VEHICLE_BENCH_MOTOR_RPM;
        break;
    case STEER_STEP_HOLD_POSITION:
        b->index = 0x607A;
        b->width = 4;
        value = b->hold;
        break;
    case STEER_STEP_SHUTDOWN:
    case STEER_STEP_SWITCH_ON:
    case STEER_STEP_ENABLE:
        b->index = 0x6040;
        value = b->step == STEER_STEP_SHUTDOWN ? 6 : b->step == STEER_STEP_SWITCH_ON ? 7 : 15;
        break;
    case STEER_STEP_READ_READY:
    case STEER_STEP_READ_SWITCHED:
    case STEER_STEP_READ_ENABLED:
        b->index = 0x6041;
        b->read = 1;
        b->expected = b->step == STEER_STEP_READ_READY      ? 0x21
                      : b->step == STEER_STEP_READ_SWITCHED ? 0x23
                                                            : 0x27;
        break;
    case STEER_STEP_TARGET:
        b->index = 0x607A;
        b->width = 4;
        value = b->desired;
        b->applied = value;
        break;
    case STEER_STEP_TRIGGER:
    case STEER_STEP_CLEAR_TRIGGER:
        b->index = 0x6040;
        value = b->step == STEER_STEP_TRIGGER ? 0x1F : 0x0F;
        break;
    case STEER_STEP_STOP:
        b->index = 0x6040;
        value = 6;
        break;
    case STEER_STEP_READ_DISABLED:
        b->index = 0x6041;
        b->read = 1;
        break;
    case STEER_STEP_WRITE_FIRST_LIMIT:
    case STEER_STEP_READ_FIRST_LIMIT:
    case STEER_STEP_WRITE_SECOND_LIMIT:
    case STEER_STEP_READ_SECOND_LIMIT:
        b->index = ((b->step < STEER_STEP_WRITE_SECOND_LIMIT) == b->upper_first) ? 0x2019 : 0x201A;
        b->width = 4;
        b->read = b->step & 1;
        value = b->index == 0x2019 ? b->maximum : b->minimum;
        b->expected = value;
        break;
    default:
        fail(b, 7U);
        return 0;
    }
    if (b->read)
        *f = PatrolCanopen_SdoRead(1, b->index, 0);
    else if (b->width == 1)
        *f = PatrolCanopen_SdoWrite8(1, b->index, 0, (uint8_t)value);
    else if (b->width == 2)
        *f = PatrolCanopen_SdoWrite16(1, b->index, 0, (uint16_t)value);
    else
        *f = PatrolCanopen_SdoWrite32(1, b->index, 0, value);
    b->pending = 1;
    b->sent_ms = now;
    return 1;
}

uint8_t BenchSteering_OnFrame(BenchSteering *b, const PatrolCanFrame *f, uint32_t now)
{
    int32_t value;
    uint8_t width;
    if (!b->pending || f->id != 0x581 || f->dlc < 4 || f->dlc > 8 || f->data[1] != (uint8_t)b->index ||
        f->data[2] != (uint8_t)(b->index >> 8) || f->data[3] != 0)
        return 0;
    if (now - b->sent_ms >= TRANSACTION_MS)
    {
        fail(b, 6U);
        return 1;
    }
    if (f->data[0] == 0x80)
    {
        if (f->dlc == 8)
            fail(b, (uint32_t)f->data[4] | (uint32_t)f->data[5] << 8 | (uint32_t)f->data[6] << 16 |
                        (uint32_t)f->data[7] << 24);
        return 1;
    }
    if (b->read)
    {
        if (!PatrolCanopen_ParseSdoValue(f, 1, b->index, 0, &value, &width) || width != b->width)
            return 1;
        if (b->step == STEER_STEP_READ_MINIMUM)
            b->previous_minimum = value;
        else if (b->step == STEER_STEP_READ_MAXIMUM)
        {
            int32_t envelope_low, envelope_high;
            if (b->preserve_limits)
            {
                if (b->previous_minimum >= value || b->hold < b->previous_minimum || b->hold > value)
                {
                    fail(b, 8U);
                    return 1;
                }
                b->minimum = b->previous_minimum;
                b->maximum = value;
                b->limits_aligned = 1U;
                b->cached_minimum = b->minimum;
                b->cached_maximum = b->maximum;
                b->pending = 0;
                b->step = STEER_STEP_WRITE_MODE;
                return 1;
            }
            if (b->previous_minimum >= value ||
                !SteeringAngle_ToControllerCount(-BENCH_STEERING_ENVELOPE_MDEG, b->center,
                                                 VEHICLE_STEERING_COUNTS_MILLI, VEHICLE_STEERING_DIRECTION,
                                                 INT32_MIN, INT32_MAX, &envelope_low) ||
                !SteeringAngle_ToControllerCount(BENCH_STEERING_ENVELOPE_MDEG, b->center,
                                                 VEHICLE_STEERING_COUNTS_MILLI, VEHICLE_STEERING_DIRECTION,
                                                 INT32_MIN, INT32_MAX, &envelope_high))
            {
                fail(b, 8U);
                return 1;
            }
            if (b->limits_aligned)
            {
                if (b->previous_minimum != b->cached_minimum || value != b->cached_maximum)
                {
                    fail(b, 8U);
                    return 1;
                }
                b->minimum = b->cached_minimum;
                b->maximum = b->cached_maximum;
            }
            else
            {
                /* The old controller limits are historical settings. This RAM-armed
                   bench capability applies the product test envelope for this power epoch. */
                b->minimum = envelope_low;
                b->maximum = envelope_high;
            }
            if (b->minimum >= b->maximum || b->hold < b->minimum || b->hold > b->maximum ||
                b->desired < b->minimum || b->desired > b->maximum)
            {
                fail(b, 8U);
                return 1;
            }
            if (!b->limits_aligned)
            {
                b->upper_first = b->minimum >= value;
                b->pending = 0;
                b->step = STEER_STEP_WRITE_FIRST_LIMIT;
                return 1;
            }
        }
        else if (b->index == 0x6041)
        {
            uint16_t state = (uint16_t)value & 0x6F;
            if ((value & 8) || (b->step == STEER_STEP_READ_DISABLED ? state == 0x27 : state != b->expected))
            {
                if (value & 8)
                    fail(b, 9U);
                else
                    b->pending = 0; /* Poll state again; bounded by prepare/stop deadline. */
                return 1;
            }
        }
        else if (value != b->expected)
        {
            fail(b, 10U);
            return 1;
        }
    }
    else if (f->data[0] != 0x60)
        return 1;
    b->pending = 0;
    b->step++;
    if (b->step == STEER_STEP_LIMITS_COMPLETE)
    {
        b->limits_aligned = 1;
        b->cached_minimum = b->minimum;
        b->cached_maximum = b->maximum;
        b->step = STEER_STEP_WRITE_MODE;
    }
    if (b->step == STEER_STEP_ACTIVE || b->step == STEER_STEP_TARGET_COMPLETE)
    {
        b->phase = BENCH_ACTIVE;
        b->step = STEER_STEP_ACTIVE;
    }
    return 1;
}
