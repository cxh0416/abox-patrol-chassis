#include "bench_drive.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(BenchDrive) == (sizeof(BenchPhase) == 1U ? 48U : 52U),
    "Drive state must preserve the fault mailbox RAM boundary");

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

enum { VERIFY_NONE, VERIFY_STATUS, VERIFY_MODE, VERIFY_LEFT, VERIFY_RIGHT, VERIFY_DRAIN };

// 锁存行走执行器首个故障并请求切断动力输出。清除在途事务、进入 BENCH_FAULT；已故障时直接返回，不用后续派生错误覆盖首因。
static void fail(BenchDrive *b, uint32_t fault)
{
    if (b->phase == BENCH_FAULT)
        return;
    b->pending = 0;
    b->verify = 0U;
    b->fault = fault;
    b->cut_power = 1;
    b->phase = BENCH_FAULT;
}
// 将带符号 rpm 转换为正转、反转或静止标志。返回值为 1、-1、0；右轮安装符号变换在报文构造阶段处理。
static int8_t direction(int16_t rpm)
{
    return (rpm > 0) - (rpm < 0);
}

// 实际转角确定内轮，比例自身保存滞回档位；进退方向不改变内轮所属侧。
void BenchDrive_SetSteeringAngle(BenchDrive* b, int32_t actual_mdeg)
{
    int64_t magnitude = llabs((int64_t)actual_mdeg);
    int percent = abs(b->turn_percent);
    if (magnitude <= BENCH_DRIVE_STRAIGHT_MDEG) { b->turn_percent = 0; return; }
    if (magnitude >= BENCH_DRIVE_LARGE_ENTER_MDEG) percent = BENCH_DRIVE_INNER_LARGE_PERCENT;
    else if (magnitude <= BENCH_DRIVE_LARGE_EXIT_MDEG || !percent) percent = BENCH_DRIVE_INNER_SMALL_PERCENT;
    b->turn_percent = (int8_t)(actual_mdeg > 0 ? percent : -percent);
}

static int16_t left_target(int16_t rpm, int8_t percent)
{
    return percent < 0 ? (int16_t)((int32_t)rpm * -percent / 100) : rpm;
}

static int16_t right_target(int16_t rpm, int8_t percent)
{
    return percent > 0 ? (int16_t)((int32_t)rpm * percent / 100) : rpm;
}

static void snapshot_pair(BenchDrive* b)
{
    b->writing = b->desired;
    b->writing_percent = b->turn_percent;
}

// 正常 ACK 和只读核验共用步骤推进。只有左右零目标均确认后才建立停车采样起点；半对目标核验后若新请求已变化，回到最新目标仲裁。
static void complete_step(BenchDrive* b, uint32_t now, uint8_t verified)
{
    b->pending = b->verify = 0U;
    if (b->step == DRIVE_STEP_TARGET_LEFT) {
        b->applied_left = left_target(b->writing, b->writing_percent);
        /* A confirmed half-pair can already move the vehicle. Preserve its
         * direction even if a new throttle request cancels the right write. */
        if (b->writing) b->direction = direction(b->writing);
    }
    else if (b->step == DRIVE_STEP_TARGET_RIGHT) b->applied_right = right_target(b->writing, b->writing_percent);
    else if (b->step == DRIVE_STEP_ZERO_LEFT || b->step == DRIVE_STEP_STOP_LEFT) b->applied_left = 0;
    else if (b->step == DRIVE_STEP_ZERO_RIGHT || b->step == DRIVE_STEP_STOP_RIGHT) b->applied_right = 0;
    if (b->step == DRIVE_STEP_READ_ENABLED) {
        b->step = DRIVE_STEP_ACTIVE;
        b->phase = BENCH_ACTIVE;
    } else if (verified && b->step == DRIVE_STEP_TARGET_LEFT && !b->reversing && b->desired != b->writing) {
        b->applied = INT16_MIN; /* Force the latest pair even if desired equals the older applied pair. */
        b->step = DRIVE_STEP_ACTIVE;
    } else if (b->step == DRIVE_STEP_TARGET_RIGHT) {
        b->step = DRIVE_STEP_ACTIVE;
        b->applied = b->writing;
        if (b->writing) b->direction = direction(b->writing);
        if (b->reversing) b->zero_ms = b->left_ms = b->right_ms = now;
    } else {
        b->step++;
        if (b->step == DRIVE_STEP_WAIT_ZERO) b->zero_ms = b->left_ms = b->right_ms = now;
    }
}

// 只对活动期双轮目标写入核验；zero_ms 在未进入停车反馈确认之前保存绝对核验起点，不改变换向总期限。
static void begin_verify(BenchDrive* b, uint32_t now)
{
    b->pending = 0U;
    b->verify = VERIFY_STATUS;
    b->zero_ms = now;
}

// 判断当前在途请求是否为可核验的活动运动写入；准备使能、模式写入和停机命令不经此路径自动恢复。
static uint8_t motion_write(const BenchDrive* b)
{
    return b->phase == BENCH_ACTIVE && !b->read &&
        (b->step == DRIVE_STEP_TARGET_LEFT || b->step == DRIVE_STEP_TARGET_RIGHT);
}
// 清空行走执行器状态并回到空闲阶段。重置握手、目标、方向和停车计数；仅修改 RAM，不发送 CAN 报文。
void BenchDrive_Init(BenchDrive *b)
{
    memset(b, 0, sizeof(*b));
}
// 从空闲状态启动行走驱动器准备流程。非空闲时拒绝；记录起始时间，实际模式写入、零速和 CiA402 使能由后续 Take/OnFrame 逐步推进。
uint8_t BenchDrive_Start(BenchDrive *b, uint32_t now)
{
    if (b->phase != BENCH_IDLE)
        return 0;
    BenchDrive_Init(b);
    b->phase = BENCH_PREPARING;
    b->started_ms = now;
    return 1;
}
// 请求行走执行器有序停车或紧急故障退出。普通停止先取消旧事务，再写左右零速、确认停车并撤使能；emergency 非零则直接锁存故障并置切电请求。
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
    b->verify = 0U;
    b->phase = BENCH_STOPPING;
    b->started_ms = now;
    b->step = DRIVE_STEP_STOP_LEFT;
    b->desired = 0;
    b->confirmations = 0;
    b->reversing = 0;
}
// 将 CAN 请求发送失败转换为行走执行故障。不盲目重发可能已部分执行的运动命令，交由运行层处理停机与供电请求。
void BenchDrive_TxFailed(BenchDrive *b)
{
    fail(b, 2);
}
// 推进行走目标、事务超时和换向停车状态。反向前明确写双轮零速，只有零速 ACK 之后且比上一对更新的左右反馈才参与三次低速确认；事务、准备、停车超时分别受限。
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
    if (b->verify)
    {
        if (now - b->zero_ms >= BENCH_WRITE_VERIFY_MS) {
            fail(b, 11U);
        } else if (b->verify != VERIFY_DRAIN && b->pending &&
                   now - b->sent_ms >= BENCH_VERIFY_READ_MS) {
            b->pending = 0U;
            b->verify = VERIFY_DRAIN;
        }
        return;
    }
    if (b->pending && motion_write(b) && now - b->sent_ms >= BENCH_WRITE_ACK_WAIT_MS)
        begin_verify(b, now);
    else if (b->pending && now - b->sent_ms >= 100U)
    {
        fail(b, 4);
        return;
    }
    if (b->phase == BENCH_PREPARING && now - b->started_ms >= 5000U)
    {
        fail(b, 5);
        return;
    }
    if (b->phase == BENCH_STOPPING && now - b->started_ms >= 6000U)
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
            if (now - b->started_ms >= 6000U)
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
            b->writing_percent = 0;
        }
        else if (rpm != b->applied || left_target(rpm,b->turn_percent) != b->applied_left ||
                 right_target(rpm,b->turn_percent) != b->applied_right)
        {
            snapshot_pair(b);
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
                    snapshot_pair(b);
                    b->step = DRIVE_STEP_TARGET_LEFT;
                }
            }
        }
    }
}
// 取出行走执行器当前步骤唯一允许发送的报文。Node 2 使用 0x60FF/0x68FF 写双轮目标，右轮符号取反；SDO 在途时不再生成新事务，取出不代表已经送达。
uint8_t BenchDrive_Take(BenchDrive *b, uint32_t now, PatrolCanFrame *f)
{
    int32_t value = 0;
    if (b->pending || b->phase == BENCH_IDLE || b->phase == BENCH_FAULT ||
        b->verify == VERIFY_DRAIN || b->step == DRIVE_STEP_ACTIVE ||
        b->step == DRIVE_STEP_WAIT_ZERO)
        return 0;
    if (b->verify) {
        if (now - b->zero_ms >= BENCH_WRITE_VERIFY_MS) { fail(b, 11U); return 0U; }
        b->index = b->verify == VERIFY_STATUS ? 0x6041U : b->verify == VERIFY_MODE ? 0x6061U :
            b->verify == VERIFY_LEFT ? 0x60FFU : 0x68FFU;
        b->read = 1U;
        b->width = b->verify == VERIFY_STATUS ? 2U : b->verify == VERIFY_MODE ? 1U : 4U;
        *f = PatrolCanopen_SdoRead(2U, b->index, 0U);
        b->pending = 1U;
        b->sent_ms = now;
        return 1U;
    }
    /* The other axis may have held the bus while this pair was queued. A
     * superseding request must go through direction/zero-speed arbitration
     * again, including when only the old left target had already been sent.
     * Angle-only changes finish the frozen pair first, so moving steering
     * feedback cannot starve right-wheel writes. */
    if (b->phase == BENCH_ACTIVE && !b->reversing && b->desired != b->writing &&
        (b->step == DRIVE_STEP_TARGET_LEFT || b->step == DRIVE_STEP_TARGET_RIGHT)) {
        b->applied = INT16_MIN;
        b->step = DRIVE_STEP_ACTIVE;
        return 0U;
    }
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
        break;
    /* The right wheel is mirror-mounted. Vehicle-forward therefore maps to
     * a positive left motor target and a negative right motor target. */
    case DRIVE_STEP_TARGET_LEFT:
    case DRIVE_STEP_TARGET_RIGHT:
        b->index = b->step == DRIVE_STEP_TARGET_LEFT ? 0x60FF : 0x68FF;
        value = b->step == DRIVE_STEP_TARGET_LEFT ? left_target(b->writing,b->writing_percent) :
            -right_target(b->writing,b->writing_percent);
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
// 处理与当前行走 SDO 事务匹配的响应。校验节点、索引、子索引、长度、时限及状态字，Abort 或模式不匹配进入故障；完成双轮零速响应后才建立停车反馈时间基准。
uint8_t BenchDrive_OnFrame(BenchDrive *b, const PatrolCanFrame *f, uint32_t now)
{
    int32_t value;
    uint8_t width;
    if (b->verify == VERIFY_DRAIN) {
        return (uint8_t)(f->id == 0x582U && f->dlc >= 4U && f->dlc <= 8U &&
                         f->data[1] == (uint8_t)b->index &&
                         f->data[2] == (uint8_t)(b->index >> 8) && f->data[3] == 0U);
    }
    if (!b->pending || f->id != 0x582 || f->dlc < 4 || f->dlc > 8 || f->data[1] != (uint8_t)b->index ||
        f->data[2] != (uint8_t)(b->index >> 8) || f->data[3])
        return 0;
    if (b->verify) {
        if (now - b->zero_ms >= BENCH_WRITE_VERIFY_MS) {
            fail(b, 11U); return 1U;
        }
        if (b->pending && now - b->sent_ms >= BENCH_VERIFY_READ_MS) {
            b->pending = 0U;
            b->verify = VERIFY_DRAIN;
            return 1U;
        }
        if (f->data[0] == 0x80U) {
            if (f->dlc == 8U) fail(b, 11U);
            return 1U;
        }
        if (!PatrolCanopen_ParseSdoValue(f, 2U, b->index, 0U, &value, &width) || width != b->width) return 1U;
        b->pending = 0U;
        if (b->verify == VERIFY_STATUS) {
            if ((value & 0x4FU) != 7U || (value & 8U)) { fail(b, 11U); return 1U; }
            b->verify = VERIFY_MODE;
        } else if (b->verify == VERIFY_MODE) {
            if (value != 3) { fail(b, 11U); return 1U; }
            b->verify = VERIFY_LEFT;
        } else {
            int32_t expected = b->verify == VERIFY_LEFT ? left_target(b->writing,b->writing_percent) :
                b->step == DRIVE_STEP_TARGET_LEFT ? -(int32_t)b->applied_right :
                -right_target(b->writing,b->writing_percent);
            if (value != expected) { fail(b, 11U); return 1U; }
            if (b->verify == VERIFY_LEFT) b->verify = VERIFY_RIGHT;
            else complete_step(b, now, 1U);
        }
        return 1U;
    }
    if (motion_write(b) && now - b->sent_ms >= BENCH_WRITE_ACK_WAIT_MS && f->data[0] != 0x80U) {
        begin_verify(b, now);
        return 1U;
    }
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
            uint8_t expected = b->step == DRIVE_STEP_READ_READY ? 1U :
                b->step == DRIVE_STEP_READ_SWITCHED ? 3U : 7U;
            if (b->step == DRIVE_STEP_READ_DISABLED ? (value & 4) != 0 : (value & 0x4F) != expected)
            {
                b->pending = 0;
                return 1;
            }
        }
        else if (value != 3) /* The only other preparation read is displayed mode 0x6061. */
        {
            fail(b, 10);
            return 1;
        }
    }
    else if (f->data[0] != 0x60)
        return 1;
    complete_step(b, now, 0U);
    return 1;
}
