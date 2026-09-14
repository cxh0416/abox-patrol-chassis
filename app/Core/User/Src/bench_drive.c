#include "bench_drive.h"
#include <stdlib.h>
#include <string.h>

/* Values are retained for diagnostic step reporting and ordered transitions. */
typedef enum
{
    DRIVE_STEP_PREOP = 0,
    DRIVE_STEP_WRITE_MODE = 1,
    DRIVE_STEP_READ_MODE = 2,
    DRIVE_STEP_ZERO_LEFT = 3,
    DRIVE_STEP_ZERO_RIGHT = 4,
    DRIVE_STEP_SHUTDOWN = 5,
    DRIVE_STEP_READ_READY = 6,
    DRIVE_STEP_SWITCH_ON = 7,
    DRIVE_STEP_READ_SWITCHED = 8,
    DRIVE_STEP_ENABLE = 9,
    DRIVE_STEP_READ_ENABLED = 10,
    DRIVE_STEP_ACTIVE = 12,
    DRIVE_STEP_TARGET_LEFT = 13,
    DRIVE_STEP_TARGET_RIGHT = 14,
    DRIVE_STEP_STOP_LEFT = 20,
    DRIVE_STEP_STOP_RIGHT = 21,
    DRIVE_STEP_WAIT_ZERO = 22,
    DRIVE_STEP_DISABLE = 23,
    DRIVE_STEP_READ_DISABLED = 24,
    DRIVE_STEP_STOP_COMPLETE = 25,
} BenchDriveStep;

static void fail(BenchDrive *b, uint32_t fault)
{
    if (b->phase == BENCH_FAULT)
        return;
    b->pending = 0;
    b->fault = fault;
    b->cut_power = 1;
    b->phase = BENCH_FAULT;
}
static int8_t direction(int16_t rpm)
{
    return (rpm > 0) - (rpm < 0);
}
void BenchDrive_Init(BenchDrive *b)
{
    memset(b, 0, sizeof(*b));
}
uint8_t BenchDrive_Start(BenchDrive *b, uint32_t now)
{
    if (b->phase != BENCH_IDLE)
        return 0;
    BenchDrive_Init(b);
    b->phase = BENCH_PREPARING;
    b->started_ms = now;
    return 1;
}
void BenchDrive_Stop(BenchDrive *b, uint32_t now, uint8_t emergency)
{
    if (emergency)
    {
        fail(b, 1);
        return;
    }
    if (b->phase == BENCH_IDLE || b->phase == BENCH_FAULT || b->phase == BENCH_STOPPING)
        return;
    /* Cancel pending motion; no write is retried after a lost acknowledgement. */
    b->pending = 0;
    b->phase = BENCH_STOPPING;
    b->started_ms = now;
    b->step = DRIVE_STEP_STOP_LEFT;
    b->desired = 0;
    b->confirmations = 0;
    b->reversing = 0;
}
void BenchDrive_TxFailed(BenchDrive *b)
{
    fail(b, 2);
}
void BenchDrive_Tick(BenchDrive *b, uint32_t now, uint8_t requested, uint8_t safe, int16_t rpm, int32_t left,
                     int32_t right, uint32_t left_ms, uint32_t right_ms)
{
    if (b->phase == BENCH_IDLE || b->phase == BENCH_FAULT)
        return;
    if (!safe || abs(rpm) > BENCH_DRIVE_MAX_RPM)
    {
        fail(b, 3);
        return;
    }
    if (!requested)
        BenchDrive_Stop(b, now, 0);
    if (b->pending && now - b->sent_ms >= 100U)
    {
        fail(b, 4);
        return;
    }
    if (b->phase == BENCH_PREPARING && now - b->started_ms >= 5000U)
    {
        fail(b, 5);
        return;
    }
    if (b->phase == BENCH_STOPPING && now - b->started_ms >= 2000U)
    {
        fail(b, 6);
        return;
    }
    if (b->phase != BENCH_STOPPING)
        b->desired = rpm;
    if (b->phase == BENCH_ACTIVE && b->step == DRIVE_STEP_ACTIVE)
    {
        if (b->reversing)
        {
            if (now - b->started_ms >= 2000U)
            {
                fail(b, 6);
                return;
            }
        }
        else if (direction(rpm) && b->direction && direction(rpm) != b->direction)
        {
            b->reversing = 1;
            b->started_ms = now;
            b->confirmations = 0;
            b->step = DRIVE_STEP_TARGET_LEFT;
            b->writing = 0; /* Always send both zero targets first. */
        }
        else if (rpm != b->applied)
        {
            b->writing = rpm;
            b->step = DRIVE_STEP_TARGET_LEFT;
        }
    }
    if ((b->reversing && b->step == DRIVE_STEP_ACTIVE) ||
        (b->phase == BENCH_STOPPING && b->step == DRIVE_STEP_WAIT_ZERO))
    {
        /* Each count requires BOTH wheels sampled after zero ACK and after the previous pair. */
        if ((int32_t)(left_ms - b->zero_ms) > 0 && (int32_t)(right_ms - b->zero_ms) > 0 &&
            (int32_t)(left_ms - b->left_ms) > 0 && (int32_t)(right_ms - b->right_ms) > 0)
        {
            b->left_ms = left_ms;
            b->right_ms = right_ms;
            if (llabs(left) <= 20 && llabs(right) <= 20)
                b->confirmations++;
            else
                b->confirmations = 0;
            if (b->confirmations >= 3)
            {
                if (b->phase == BENCH_STOPPING)
                    b->step = DRIVE_STEP_DISABLE;
                else
                {
                    b->reversing = 0;
                    b->direction = 0;
                    b->writing = b->desired;
                    b->step = DRIVE_STEP_TARGET_LEFT;
                }
            }
        }
    }
}
uint8_t BenchDrive_Take(BenchDrive *b, uint32_t now, PatrolCanFrame *f)
{
    int32_t value = 0;
    if (b->pending || b->phase == BENCH_IDLE || b->phase == BENCH_FAULT || b->step == DRIVE_STEP_ACTIVE ||
        b->step == DRIVE_STEP_WAIT_ZERO)
        return 0;
    if (b->step == DRIVE_STEP_PREOP || b->step == DRIVE_STEP_STOP_COMPLETE)
    {
        /* Vendor dual-drive examples select Pre-Operational for SDO control.
         * No PDO mapping has been approved; do not start unsolicited PDO traffic. */
        memset(f, 0, sizeof(*f));
        f->dlc = 2;
        f->data[0] = 0x80;
        f->data[1] = 2;
        if (b->step == DRIVE_STEP_STOP_COMPLETE)
            b->phase = BENCH_IDLE;
        else
            b->step++;
        return 1;
    }
    b->read = 0;
    b->width = 4;
    switch (b->step)
    {
    case DRIVE_STEP_WRITE_MODE:
        b->index = 0x6060;
        b->width = 1;
        value = 3;
        break;
    case DRIVE_STEP_READ_MODE:
        b->index = 0x6061;
        b->width = 1;
        b->read = 1;
        b->expected = 3;
        break;
    case DRIVE_STEP_ZERO_LEFT:
    case DRIVE_STEP_ZERO_RIGHT:
        b->index = b->step == DRIVE_STEP_ZERO_LEFT ? 0x60FF : 0x68FF;
        break;
    case DRIVE_STEP_SHUTDOWN:
    case DRIVE_STEP_SWITCH_ON:
    case DRIVE_STEP_ENABLE:
        b->index = 0x6040;
        b->width = 2;
        value = b->step == DRIVE_STEP_SHUTDOWN ? 6 : b->step == DRIVE_STEP_SWITCH_ON ? 7 : 15;
        break;
    /* VSY dual: bits 0/1/2 are Ready/Switched/Enabled (vendor PDF p24).
     * Observed Ready=0x1001; unlike Node 1, bit 5 is not set here. */
    case DRIVE_STEP_READ_READY:
    case DRIVE_STEP_READ_SWITCHED:
    case DRIVE_STEP_READ_ENABLED:
        b->index = 0x6041;
        b->width = 2;
        b->read = 1;
        b->expected = b->step == DRIVE_STEP_READ_READY ? 1 : b->step == DRIVE_STEP_READ_SWITCHED ? 3 : 7;
        break;
    /* The right wheel is mirror-mounted. Vehicle-forward therefore maps to
     * a positive left motor target and a negative right motor target. */
    case DRIVE_STEP_TARGET_LEFT:
    case DRIVE_STEP_TARGET_RIGHT:
        b->index = b->step == DRIVE_STEP_TARGET_LEFT ? 0x60FF : 0x68FF;
        value = b->step == DRIVE_STEP_TARGET_LEFT ? b->writing : -b->writing;
        break;
    case DRIVE_STEP_STOP_LEFT:
    case DRIVE_STEP_STOP_RIGHT:
        b->index = b->step == DRIVE_STEP_STOP_LEFT ? 0x60FF : 0x68FF;
        break;
    case DRIVE_STEP_DISABLE:
        b->index = 0x6040;
        b->width = 2;
        value = 6;
        break;
    case DRIVE_STEP_READ_DISABLED:
        b->index = 0x6041;
        b->width = 2;
        b->read = 1;
        break;
    default:
        fail(b, 7);
        return 0;
    }
    *f = b->read         ? PatrolCanopen_SdoRead(2, b->index, 0)
         : b->width == 1 ? PatrolCanopen_SdoWrite8(2, b->index, 0, (uint8_t)value)
         : b->width == 2 ? PatrolCanopen_SdoWrite16(2, b->index, 0, (uint16_t)value)
                         : PatrolCanopen_SdoWrite32(2, b->index, 0, value);
    b->pending = 1;
    b->sent_ms = now;
    return 1;
}
uint8_t BenchDrive_OnFrame(BenchDrive *b, const PatrolCanFrame *f, uint32_t now)
{
    int32_t value;
    uint8_t width;
    if (!b->pending || f->id != 0x582 || f->dlc < 4 || f->dlc > 8 || f->data[1] != (uint8_t)b->index ||
        f->data[2] != (uint8_t)(b->index >> 8) || f->data[3])
        return 0;
    if (now - b->sent_ms >= 100U)
    {
        fail(b, 4);
        return 1;
    }
    if (f->data[0] == 0x80)
    {
        if (f->dlc == 8)
            fail(b, 8);
        return 1;
    }
    if (b->read)
    {
        if (!PatrolCanopen_ParseSdoValue(f, 2, b->index, 0, &value, &width) || width != b->width)
            return 1;
        if (b->index == 0x6041)
        {
            if (value & 8)
            {
                fail(b, 9);
                return 1;
            }
            if (b->step == DRIVE_STEP_READ_DISABLED ? (value & 4) != 0 : (value & 0x4F) != b->expected)
            {
                b->pending = 0;
                return 1;
            }
        }
        else if (value != b->expected)
        {
            fail(b, 10);
            return 1;
        }
    }
    else if (f->data[0] != 0x60)
        return 1;
    b->pending = 0;
    if (b->step == DRIVE_STEP_READ_ENABLED)
    {
        b->step = DRIVE_STEP_ACTIVE;
        b->phase = BENCH_ACTIVE;
    }
    else if (b->step == DRIVE_STEP_TARGET_RIGHT)
    {
        b->step = DRIVE_STEP_ACTIVE;
        b->applied = b->writing;
        if (b->writing)
            b->direction = direction(b->writing);
        if (b->reversing)
        {
            b->zero_ms = now;
            b->left_ms = now;
            b->right_ms = now;
        }
    }
    else
    {
        b->step++;
        if (b->step == DRIVE_STEP_WAIT_ZERO)
        {
            b->zero_ms = now;
            b->left_ms = now;
            b->right_ms = now;
        }
    }
    return 1;
}
