#include "chassis_control.h"
#include "vehicle_steering_profile.h"

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
    if (reason == CHASSIS_STOP_DIRECTION_TIMEOUT ||
        reason == CHASSIS_STOP_ENCODER_RANGE ||
        reason == CHASSIS_STOP_ENCODER_PROFILE) control->fault_latched = 1U;
    if (release) {
        control->session_active = 0U;
        control->session_id[0] = '\0';
    }
}

void ChassisControl_Init(ChassisControl* control)
{
    memset(control, 0, sizeof(*control));
    control->stop_reason = CHASSIS_STOP_UNCALIBRATED;
}

void ChassisControl_SetFeedback(ChassisControl* control, const ChassisFeedback* feedback)
{
    control->feedback = *feedback;
}

static uint8_t steering_hard_range(const ChassisFeedback* f)
{
    return llabs((int64_t)f->steering_actual_mdeg)<=VEHICLE_STEERING_TRIP_MDEG &&
           llabs((int64_t)f->encoder_actual_mdeg)<=VEHICLE_STEERING_TRIP_MDEG;
}
static void steering_boundary(ChassisControl* c, int32_t requested, uint8_t allow_clear)
{
    const ChassisFeedback* f=&c->feedback;
    if (f->steering_actual_mdeg>CHASSIS_MAX_STEERING_MDEG || f->encoder_actual_mdeg>CHASSIS_MAX_STEERING_MDEG)
        c->steering_boundary=1;
    else if (f->steering_actual_mdeg<-CHASSIS_MAX_STEERING_MDEG || f->encoder_actual_mdeg<-CHASSIS_MAX_STEERING_MDEG)
        c->steering_boundary=-1;
    else if (allow_clear && llabs((int64_t)requested)<=VEHICLE_STEERING_RECOVER_MDEG &&
             llabs((int64_t)f->steering_actual_mdeg)<=VEHICLE_STEERING_RECOVER_MDEG &&
             llabs((int64_t)f->encoder_actual_mdeg)<=VEHICLE_STEERING_RECOVER_MDEG)
        c->steering_boundary=0;
    if (!c->steering_boundary) return;
    c->target_rpm=0; c->requested_rpm=0; c->reversal_pending=0; c->zero_confirmations=0;
    if (c->steering_boundary>0 && c->target_steering_mdeg>VEHICLE_STEERING_RECOVER_MDEG)
        c->target_steering_mdeg=VEHICLE_STEERING_RECOVER_MDEG;
    if (c->steering_boundary<0 && c->target_steering_mdeg<-VEHICLE_STEERING_RECOVER_MDEG)
        c->target_steering_mdeg=-VEHICLE_STEERING_RECOVER_MDEG;
}

uint8_t ChassisControl_SafetyReady(const ChassisControl* control, uint32_t now_ms)
{
    return ChassisControl_SafetyReason(control, now_ms) == CHASSIS_STOP_NONE;
}

ChassisStopReason ChassisControl_RuntimeSafetyReason(const ChassisControl* control, uint32_t now_ms)
{
    const ChassisFeedback* f = &control->feedback;
    if (control->fault_latched) return control->stop_reason;
    if (!f->steering_online || !f->drive_online) return CHASSIS_STOP_NODE_OFFLINE;
    if (f->can_bus_off) return CHASSIS_STOP_CAN_BUS_OFF;
    if (f->drive_fault) return CHASSIS_STOP_DRIVE_FAULT;
    if (f->ota_busy) return CHASSIS_STOP_OTA_BUSY;
    if ((now_ms - f->feedback_ms) > CHASSIS_FEEDBACK_TIMEOUT_MS) return CHASSIS_STOP_FEEDBACK_STALE;
    return CHASSIS_STOP_NONE;
}

ChassisStopReason ChassisControl_SafetyReason(const ChassisControl* control, uint32_t now_ms)
{
    const ChassisFeedback* f = &control->feedback;
    ChassisStopReason reason = ChassisControl_RuntimeSafetyReason(control, now_ms);
    if (reason != CHASSIS_STOP_NONE) return reason;
    if (!f->calibrated) return CHASSIS_STOP_UNCALIBRATED;
    if (!f->encoder_profile_ready) return CHASSIS_STOP_ENCODER_PROFILE;
    if (!f->encoder_range_valid || !steering_hard_range(f)) return CHASSIS_STOP_ENCODER_RANGE;
    if (!f->power_ready) return CHASSIS_STOP_POWER_NOT_READY;
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
        uint8_t release = (uint8_t)(!control->session_active ||
            strcmp(control->session_id, session_id) == 0);
        force_stop(control, CHASSIS_STOP_EXPLICIT, release);
        control->auto_center_inhibited = 1U;
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
    if (seq <= control->last_seq || ChassisControl_SafetyReason(control, now_ms) != CHASSIS_STOP_NONE) {
        force_stop(control, ChassisControl_SafetyReason(control, now_ms), 0U);
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
    steering_boundary(control, steering_mdeg, 1U);
    control->enable_request = 1U;
    control->auto_center_inhibited = 0U;
    control->stop_reason = CHASSIS_STOP_NONE;
    return 1U;
}

void ChassisControl_Tick(ChassisControl* control, uint32_t now_ms)
{
    ChassisStopReason reason = ChassisControl_SafetyReason(control, now_ms);
    if (reason != CHASSIS_STOP_NONE) { force_stop(control, reason, 0U); return; }
    if (control->session_active && (int32_t)(now_ms - control->lease_deadline_ms) >= 0) {
        force_stop(control, CHASSIS_STOP_LEASE_EXPIRED, 1U);
        return;
    }
    steering_boundary(control, control->target_steering_mdeg, 0U);
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
