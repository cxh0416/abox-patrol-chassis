#include "chassis_control.h"

#include <stdlib.h>
#include <string.h>

static int8_t direction_of(int16_t rpm) { return (rpm > 0) ? 1 : ((rpm < 0) ? -1 : 0); }
static uint8_t stopped(const ChassisFeedback* feedback)
{
    return (abs(feedback->left_actual_rpm) <= 20 && abs(feedback->right_actual_rpm) <= 20) ? 1U : 0U;
}

static void force_stop(ChassisControl* control, ChassisStopReason reason, uint8_t release)
{
    control->target_rpm = 0;
    control->target_steering_mdeg = 0;
    control->requested_rpm = 0;
    control->reversal_pending = 0U;
    control->zero_confirmations = 0U;
    control->enable_request = 0U;
    if (!control->fault_latched) control->stop_reason = reason;
    if (reason == CHASSIS_STOP_DIRECTION_TIMEOUT) control->fault_latched = 1U;
    if (release) {
        control->session_active = 0U;
        control->session_id[0] = '\0';
    }
}

void ChassisControl_Init(ChassisControl* control, uint8_t remote_enabled)
{
    memset(control, 0, sizeof(*control));
    control->remote_enabled = remote_enabled ? 1U : 0U;
    control->stop_reason = remote_enabled ? CHASSIS_STOP_UNCALIBRATED : CHASSIS_STOP_REMOTE_DISABLED;
}

void ChassisControl_SetFeedback(ChassisControl* control, const ChassisFeedback* feedback)
{
    control->feedback = *feedback;
}

uint8_t ChassisControl_SafetyReady(const ChassisControl* control, uint32_t now_ms)
{
    const ChassisFeedback* f = &control->feedback;
    if (control->fault_latched || !control->remote_enabled || !f->calibrated || !f->steering_online || !f->drive_online ||
        f->can_bus_off || f->drive_fault || f->ota_busy || !f->power_ready) return 0U;
    if ((now_ms - f->feedback_ms) > CHASSIS_FEEDBACK_TIMEOUT_MS) return 0U;
    if (llabs((int64_t)f->steering_actual_mdeg - f->encoder_actual_mdeg) > 2000L) return 0U;
    return 1U;
}

static ChassisStopReason safety_reason(const ChassisControl* control, uint32_t now_ms)
{
    const ChassisFeedback* f = &control->feedback;
    if (control->fault_latched) return control->stop_reason;
    if (!control->remote_enabled) return CHASSIS_STOP_REMOTE_DISABLED;
    if (!f->calibrated) return CHASSIS_STOP_UNCALIBRATED;
    if (!f->steering_online || !f->drive_online) return CHASSIS_STOP_NODE_OFFLINE;
    if (f->can_bus_off) return CHASSIS_STOP_CAN_BUS_OFF;
    if (f->drive_fault) return CHASSIS_STOP_DRIVE_FAULT;
    if (f->ota_busy) return CHASSIS_STOP_OTA_BUSY;
    if (!f->power_ready) return CHASSIS_STOP_POWER_NOT_READY;
    if ((now_ms - f->feedback_ms) > CHASSIS_FEEDBACK_TIMEOUT_MS) return CHASSIS_STOP_FEEDBACK_STALE;
    if (llabs((int64_t)f->steering_actual_mdeg - f->encoder_actual_mdeg) > 2000L) return CHASSIS_STOP_ANGLE_MISMATCH;
    return CHASSIS_STOP_NONE;
}

uint8_t ChassisControl_Apply(ChassisControl* control, const char* action, const char* session_id,
                             uint32_t seq, uint32_t lease_ms, int16_t drive_rpm,
                             int32_t steering_mdeg, uint32_t now_ms)
{
    int8_t requested_direction;
    if (action == 0 || session_id == 0 || session_id[0] == '\0') return 0U;
    if (strcmp(action, "stop") == 0) {
        if (control->session_active && strcmp(control->session_id, session_id) != 0) return 0U;
        force_stop(control, CHASSIS_STOP_EXPLICIT, 1U);
        return 1U;
    }
    if (strcmp(action, "acquire") != 0 && strcmp(action, "renew") != 0) return 0U;
    if (lease_ms == 0U || lease_ms > CHASSIS_MAX_LEASE_MS || abs(drive_rpm) > CHASSIS_MAX_RPM ||
        labs(steering_mdeg) > CHASSIS_MAX_STEERING_MDEG) return 0U;
    if (strcmp(action, "acquire") == 0) {
        if (control->session_active && strcmp(control->session_id, session_id) != 0) return 0U;
        strncpy(control->session_id, session_id, sizeof(control->session_id) - 1U);
        control->session_id[sizeof(control->session_id) - 1U] = '\0';
        control->session_active = 1U;
        control->last_seq = 0U;
    } else if (!control->session_active || strcmp(control->session_id, session_id) != 0) return 0U;
    if (seq <= control->last_seq || safety_reason(control, now_ms) != CHASSIS_STOP_NONE) {
        force_stop(control, safety_reason(control, now_ms), 0U);
        return 0U;
    }
    control->last_seq = seq;
    control->lease_deadline_ms = now_ms + lease_ms;
    control->target_steering_mdeg = steering_mdeg;
    requested_direction = direction_of(drive_rpm);
    if (requested_direction != 0 && control->active_direction != 0 && requested_direction != control->active_direction) {
        control->requested_rpm = drive_rpm;
        control->target_rpm = 0;
        if (!control->reversal_pending) {
            control->reversal_pending = 1U;
            control->zero_confirmations = 0U;
            control->reversal_started_ms = now_ms;
            control->last_zero_feedback_ms = control->feedback.feedback_ms;
        }
    } else {
        control->requested_rpm = drive_rpm;
        control->target_rpm = drive_rpm;
        if (requested_direction != 0) control->active_direction = requested_direction;
    }
    control->enable_request = 1U;
    control->stop_reason = CHASSIS_STOP_NONE;
    return 1U;
}

void ChassisControl_Tick(ChassisControl* control, uint32_t now_ms)
{
    ChassisStopReason reason = safety_reason(control, now_ms);
    if (reason != CHASSIS_STOP_NONE) { force_stop(control, reason, 0U); return; }
    if (control->session_active && (int32_t)(now_ms - control->lease_deadline_ms) >= 0) {
        force_stop(control, CHASSIS_STOP_LEASE_EXPIRED, 1U);
        return;
    }
    if (!control->reversal_pending) return;
    control->target_rpm = 0;
    if (control->feedback.feedback_ms != control->last_zero_feedback_ms) {
        control->last_zero_feedback_ms = control->feedback.feedback_ms;
        if (stopped(&control->feedback)) {
            if (++control->zero_confirmations >= 3U) {
                control->target_rpm = control->requested_rpm;
                control->active_direction = direction_of(control->requested_rpm);
                control->reversal_pending = 0U;
                control->zero_confirmations = 0U;
            }
        } else control->zero_confirmations = 0U;
    }
    if (control->reversal_pending && (now_ms - control->reversal_started_ms) >= 2000U)
        force_stop(control, CHASSIS_STOP_DIRECTION_TIMEOUT, 1U);
}
