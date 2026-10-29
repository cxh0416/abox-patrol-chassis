#include "steering_angle.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "vehicle_steering_profile.h"

// 计算带绝对计数环绕处理的中位差值。采用宽位数中间值选择有符号相对差，避免直接无符号相减造成错误转向角。
static int64_t absolute_delta(uint32_t count, uint32_t center)
{
    int64_t delta = (int64_t)count - center;
    if (count < VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS && center < VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS) {
        if (delta > VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS / 2) delta -= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS;
        else if (delta < -(int64_t)(VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS / 2)) delta += VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS;
    }
    return delta;
}

// 使当前上电周期的转向运行坐标失效。清除中位建立与稳定样本，不修改 Flash 中的机械标定参数。
void SteeringCoordinate_Reset(SteeringCoordinate* c) { memset(c, 0, sizeof(*c)); }
// 用一致的驱动器和编码器反馈建立本次运行中位。仅在 ready 条件与独立采样要求满足时推进；运行坐标用于处理驱动器位置计数上电变化，不等同于重新标定。
void SteeringCoordinate_Observe(SteeringCoordinate* c, uint8_t ready,
    int32_t driver, uint32_t absolute, uint32_t absolute_center, uint32_t sample_ms, uint32_t now)
{
    int64_t offset, center;
    if (c->valid) return; /* Never hide a disagreement by re-zeroing while running. */
    if (!ready || absolute >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS ||
        absolute_center >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS) { c->tracking = 0; return; }
    if (!c->tracking || llabs((int64_t)driver - c->anchor_driver) > 27 ||
        llabs(absolute_delta(absolute, c->anchor_absolute)) > 1) {
        c->tracking = 1; c->samples = 1; c->quiet_since = now;
        c->anchor_driver = driver; c->anchor_absolute = absolute; c->last_sample_ms = sample_ms;
        return;
    }
    if (sample_ms == c->last_sample_ms) return;
    c->last_sample_ms = sample_ms;
    if (c->samples < 255) c->samples++;
    if (c->samples < 3 || now - c->quiet_since < 300U) return;
    offset = absolute_delta(absolute, absolute_center) * VEHICLE_STEERING_COUNTS_MILLI *
        VEHICLE_ABSOLUTE_DIRECTION * VEHICLE_STEERING_DIRECTION / VEHICLE_ABSOLUTE_COUNTS_MILLI;
    center = (int64_t)driver - offset;
    if (center < INT32_MIN || center > INT32_MAX) return;
    c->runtime_center = (int32_t)center;
    c->valid = 1;
}

// 判断已有运行坐标是否因反馈或标定变化失效。根据门禁、节点在线和标定一致性判断，失效后应走安全退出而非带使能重新置零。
uint8_t SteeringCoordinate_EpochLost(uint16_t feedback_gate_mask, uint8_t steering_online,
    uint8_t calibration_unchanged)
{
    /* Only loss of a position source destroys the live driver/encoder
     * relationship.  Drive feedback/status and other motion gates still block
     * motion, but they must not invalidate an idle steering coordinate. */
    const uint16_t position_source_mask = 1U | 4U | 32U;
    return ((feedback_gate_mask & position_source_mask) != 0U || !steering_online ||
            !calibration_unchanged) ? 1U : 0U;
}

// 将毫度目标转换为带方向的驱动器位置计数。使用 64 位中间量并检查比例、方向及计数限位；仅在转换成功时向 target 提交结果。
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

// 把相对计数差换算为带符号毫度角。根据每度计数比例和方向计算，处理无效比例及返回类型边界。
static int32_t from_delta(int64_t delta, uint32_t counts_per_degree_milli, int8_t direction)
{
    int64_t angle;
    if (counts_per_degree_milli == 0U || (direction != 1 && direction != -1)) return 0;
    angle = delta * 1000000LL * direction / counts_per_degree_milli;
    if (angle > INT32_MAX) return INT32_MAX;
    if (angle < INT32_MIN) return INT32_MIN;
    return (int32_t)angle;
}

// 将驱动器位置计数相对中位换算为毫度。内部使用宽位差值，方向和比例来自本车标定，不与编码器计数直接混用。
int32_t SteeringAngle_FromControllerCount(int32_t count, int32_t center,
                                          uint32_t counts_per_degree_milli,
                                          int8_t direction)
{
    return from_delta((int64_t)count - (int64_t)center, counts_per_degree_milli, direction);
}

// 将绝对编码器计数相对中位换算为毫度。先按编码器计数特性求差，再统一使用标定比例与方向。
int32_t SteeringAngle_FromAbsoluteCount(uint32_t count, uint32_t center,
                                        uint32_t counts_per_degree_milli,
                                        int8_t direction)
{
    return from_delta(absolute_delta(count, center), counts_per_degree_milli, direction);
}

// 验证绝对编码器参数并检查角度范围。参数合法时先写回换算角度，即使超限也会输出该角度并返回零；参数非法则不写输出。
uint8_t SteeringAngle_AbsoluteWithinLimit(uint32_t count, uint32_t center,
    uint32_t counts_per_degree_milli, int8_t direction, int32_t limit_mdeg,
    int32_t* angle_mdeg)
{
    int32_t angle;
    if (angle_mdeg == 0 || count >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS ||
        center >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS || counts_per_degree_milli == 0U ||
        (direction != 1 && direction != -1) || limit_mdeg < 0) return 0U;
    angle = SteeringAngle_FromAbsoluteCount(count, center, counts_per_degree_milli, direction);
    *angle_mdeg = angle;
    return (angle >= -limit_mdeg && angle <= limit_mdeg) ? 1U : 0U;
}

// 将连续单圈绝对计数展开为累计相对计数。根据单圈模数处理过零，拒绝超过 max_step_counts 的不可信跳变，避免误判为正常转向位移。
uint8_t SteeringAngle_AccumulateSingleTurn(uint32_t previous, uint32_t current,
    uint32_t single_turn_counts, uint32_t max_step_counts, int32_t* accumulated)
{
    int64_t delta, next;
    if (accumulated == NULL || single_turn_counts < 2U || previous >= single_turn_counts ||
        current >= single_turn_counts || max_step_counts == 0U || max_step_counts > INT32_MAX)
        return 0U;
    delta = (int64_t)current - (int64_t)previous;
    if (delta > (int64_t)single_turn_counts / 2LL) delta -= single_turn_counts;
    else if (delta < -(int64_t)single_turn_counts / 2LL) delta += single_turn_counts;
    if (llabs(delta) > max_step_counts) return 0U;
    next = (int64_t)*accumulated + delta;
    if (next < INT32_MIN || next > INT32_MAX) return 0U;
    *accumulated = (int32_t)next;
    return 1U;
}
