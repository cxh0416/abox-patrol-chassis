#include "patrol_auto_center.h"

#include <stdlib.h>
#include <string.h>

// 登记一次本地自动回中请求并保存保持角度。记录起始时间及零速确认上下文，先等待行走停稳再允许转向回中。
void PatrolAutoCenter_Request(PatrolAutoCenter* center, uint32_t now, int32_t hold_mdeg)
{
    memset(center, 0, sizeof(*center));
    center->requested = 1U;
    center->started_ms = now;
    /* Boundary-buffer feedback must not become an out-of-range hold target. */
    center->hold_mdeg = hold_mdeg > CHASSIS_MAX_STEERING_MDEG ? CHASSIS_MAX_STEERING_MDEG :
        hold_mdeg < -CHASSIS_MAX_STEERING_MDEG ? -CHASSIS_MAX_STEERING_MDEG : hold_mdeg;
}

// 撤销本地回中请求并重置其阶段状态。用于远控或标定接管；不直接写驱动器，后续运行调度决定实际目标。
void PatrolAutoCenter_Cancel(PatrolAutoCenter* center)
{
    memset(center, 0, sizeof(*center));
}

// 推进本地回中的等待、运动和到位确认阶段。要求安全门禁和行走停放状态，结合新鲜双角度反馈确认到位；失败保留 failed 供运行层故障处理。
uint8_t PatrolAutoCenter_Tick(PatrolAutoCenter* center, uint32_t now,
    uint8_t safe, uint8_t drive_parked, const ChassisFeedback* feedback)
{
    if (!center->requested) return 0U;
    if (now - center->started_ms >= PATROL_AUTO_CENTER_TIMEOUT_MS) {
        PatrolAutoCenter_Cancel(center);
        center->failed = 1U;
        return 0U;
    }
    if (!safe ||
        now - feedback->feedback_ms > CHASSIS_FEEDBACK_TIMEOUT_MS) {
        PatrolAutoCenter_Cancel(center);
        return 0U;
    }
    if (!drive_parked || abs(feedback->left_actual_rpm) > 20 ||
        abs(feedback->right_actual_rpm) > 20) {
        center->centered_samples = 0U;
        center->moving = 0U;
        return 0U;
    }
    if (feedback->feedback_ms != center->last_sample_ms) {
        center->last_sample_ms = feedback->feedback_ms;
        /* Completion keeps the existing three dual-center samples, with
         * comparable timestamps; transient skew never stops the return. */
        if (feedback->angle_pair_valid &&
            llabs((int64_t)feedback->steering_actual_mdeg) <= PATROL_AUTO_CENTER_TOLERANCE_MDEG &&
            llabs((int64_t)feedback->encoder_actual_mdeg) <= PATROL_AUTO_CENTER_TOLERANCE_MDEG)
            ++center->centered_samples;
        else
            center->centered_samples = 0U;
    }
    if (center->centered_samples >= 3U) {
        PatrolAutoCenter_Cancel(center);
        return 0U;
    }
    center->moving = 1U;
    return 1U;
}
