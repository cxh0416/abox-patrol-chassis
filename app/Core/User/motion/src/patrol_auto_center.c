#include "patrol_auto_center.h"

#include <stdlib.h>
#include <string.h>

void PatrolAutoCenter_Request(PatrolAutoCenter* center, uint32_t now, int32_t hold_mdeg)
{
    memset(center, 0, sizeof(*center));
    center->requested = 1U;
    center->started_ms = now;
    /* Boundary-buffer feedback must not become an out-of-range hold target. */
    center->hold_mdeg = hold_mdeg > CHASSIS_MAX_STEERING_MDEG ? CHASSIS_MAX_STEERING_MDEG :
        hold_mdeg < -CHASSIS_MAX_STEERING_MDEG ? -CHASSIS_MAX_STEERING_MDEG : hold_mdeg;
}

void PatrolAutoCenter_Cancel(PatrolAutoCenter* center)
{
    memset(center, 0, sizeof(*center));
}

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
        if (llabs((int64_t)feedback->steering_actual_mdeg) <= PATROL_AUTO_CENTER_TOLERANCE_MDEG &&
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
