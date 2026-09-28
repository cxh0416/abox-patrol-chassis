#include "motion_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_log.h"
#include "remote_service.h"
#include "patrol_config.h"
#include "vehicle_power_output.h"
#include "vehicle_steering_profile.h"

/* READY uses a human-scale idle timeout, independent of status polling. */
#define CENTER_CAL_READY_IDLE_MS 600000U
#define CENTER_CAL_DONE_OBSERVE_MS 60000U
#ifndef PATROL_CHASSIS_ACTUATION_ENABLED
#define PATROL_CHASSIS_ACTUATION_ENABLED 0
#endif

uint8_t CenterCalibration_IsActive(void)
{
    return g_motion.center_cal.phase != CENTER_CAL_IDLE &&
        g_motion.center_cal.phase != CENTER_CAL_COMPLETE &&
        g_motion.center_cal.phase != CENTER_CAL_FAILED && g_motion.center_cal.phase != CENTER_CAL_EXPIRED;
}

static const char* center_reason_name(uint8_t reason)
{
    switch ((CenterCalibrationReason)reason) {
    case CENTER_CAL_REASON_READY_LEASE_TIMEOUT: return "ready_lease_timeout";
    case CENTER_CAL_REASON_PHASE_TIMEOUT: return "phase_timeout";
    case CENTER_CAL_REASON_GATE_NOT_READY: return "gate_not_ready";
    case CENTER_CAL_REASON_RELATIVE_MISMATCH: return "relative_mismatch";
    case CENTER_CAL_REASON_BENCH_EXECUTOR_FAULT: return "steering_executor_fault";
    case CENTER_CAL_REASON_COMMIT_BENCH_BUSY: return "commit_executor_busy";
    case CENTER_CAL_REASON_COMMIT_STEERING_ENABLED: return "commit_steering_enabled";
    case CENTER_CAL_REASON_CONFIG_CLEAR_FAILED: return "config_clear_failed";
    case CENTER_CAL_REASON_ENCODER_TX_FAILED: return "encoder_tx_failed";
    case CENTER_CAL_REASON_ENCODER_VERIFY_MISMATCH: return "encoder_verify_mismatch";
    case CENTER_CAL_REASON_CONFIG_SAVE_FAILED: return "config_save_failed";
    case CENTER_CAL_REASON_WRONG_PHASE: return "wrong_phase";
    case CENTER_CAL_REASON_SESSION_MISMATCH: return "session_mismatch";
    case CENTER_CAL_REASON_INVALID_JOG_DELTA: return "invalid_jog_delta";
    case CENTER_CAL_REASON_JOG_LIMIT: return "jog_limit";
    case CENTER_CAL_REASON_ALREADY_ACTIVE: return "already_active";
    case CENTER_CAL_REASON_ENCODER_OUT_OF_RANGE: return "encoder_out_of_range";
    case CENTER_CAL_REASON_EXECUTOR_BUSY: return "executor_busy";
    case CENTER_CAL_REASON_POSITION_EPOCH_RESET: return "position_epoch_reset";
    default: return "none";
    }
}

static const char* center_business_reason(uint8_t reason)
{
    switch ((CenterCalibrationReason)reason) {
    case CENTER_CAL_REASON_NONE: return "NONE";
    case CENTER_CAL_REASON_READY_LEASE_TIMEOUT: return "EXPIRED";
    case CENTER_CAL_REASON_WRONG_PHASE:
    case CENTER_CAL_REASON_ALREADY_ACTIVE: return "INVALID_STATE";
    case CENTER_CAL_REASON_SESSION_MISMATCH: return "SESSION_MISMATCH";
    case CENTER_CAL_REASON_GATE_NOT_READY:
    case CENTER_CAL_REASON_EXECUTOR_BUSY:
    case CENTER_CAL_REASON_COMMIT_BENCH_BUSY:
    case CENTER_CAL_REASON_COMMIT_STEERING_ENABLED: return "SAFETY_BLOCKED";
    case CENTER_CAL_REASON_INVALID_JOG_DELTA:
    case CENTER_CAL_REASON_JOG_LIMIT:
    case CENTER_CAL_REASON_ENCODER_OUT_OF_RANGE: return "OUT_OF_RANGE";
    case CENTER_CAL_REASON_CONFIG_CLEAR_FAILED:
    case CENTER_CAL_REASON_CONFIG_SAVE_FAILED: return "PERSIST_FAILED";
    default: return "EXECUTION_FAILED";
    }
}

uint32_t CenterCalibration_GateMask(const PatrolDiagnosticsSnapshot* s,
                                 uint8_t require_steering_disabled)
{
    const ChassisControl* c = RemoteService_State();
    uint32_t mask = 0U;
    if (s == NULL || c == NULL) return UINT32_MAX;
    if (!PATROL_CHASSIS_ACTUATION_ENABLED) mask |= CENTER_GATE_ACTUATION_DISABLED;
    if (!VehiclePowerOutput_IsOn()) mask |= CENTER_GATE_POWER_OFF;
    if (PatrolRuntimePort_CanBusOff()) mask |= CENTER_GATE_CAN_BUS_OFF;
    if (PatrolRuntimePort_CanBusOffActive()) mask |= CENTER_GATE_CAN_BUS_OFF_ACTIVE;
    if (PatrolRuntimePort_OtaBusy()) mask |= CENTER_GATE_OTA_BUSY;
    if (!s->steering_online) mask |= CENTER_GATE_STEERING_OFFLINE;
    if (!s->drive_online) mask |= CENTER_GATE_DRIVE_OFFLINE;
    if (!s->steering_feedback_fresh) mask |= CENTER_GATE_STEERING_FEEDBACK_STALE;
    if (!s->drive_feedback_fresh) mask |= CENTER_GATE_DRIVE_FEEDBACK_STALE;
    if (!s->steering_status_fresh) mask |= CENTER_GATE_STEERING_STATUS_STALE;
    if (!s->drive_status_fresh) mask |= CENTER_GATE_DRIVE_STATUS_STALE;
    if (!s->encoder_online) mask |= CENTER_GATE_ENCODER_OFFLINE;
    if (!s->encoder_config_valid) mask |= CENTER_GATE_ENCODER_CONFIG_INVALID;
    if (s->drive_fault) mask |= CENTER_GATE_DRIVE_FAULT;
    if (labs(s->left_actual_rpm) > 20L || labs(s->right_actual_rpm) > 20L)
        mask |= CENTER_GATE_WHEELS_MOVING;
    if (c->session_active) mask |= CENTER_GATE_CONTROL_SESSION_ACTIVE;
    if (c->enable_request) mask |= CENTER_GATE_CONTROL_ENABLE_ACTIVE;
    if ((s->drive_status_word & 0x006FU) == 0x0027U)
        mask |= CENTER_GATE_DRIVE_OPERATION_ENABLED;
    if (c->fault_latched) mask |= CENTER_GATE_FAULT_LATCHED;
    if (ChassisControl_RuntimeSafetyReason(c, PatrolRuntimePort_Now()) != CHASSIS_STOP_NONE)
        mask |= CENTER_GATE_RUNTIME_UNSAFE;
    if (require_steering_disabled && (s->steering_status_word & 0x006FU) == 0x0027U)
        mask |= CENTER_GATE_STEERING_OPERATION_ENABLED;
    return mask;
}

static void center_reject_locked(CenterCalibrationReason reason, uint32_t gate_mask)
{
    g_motion.center_cal.last_reject_reason = (uint8_t)reason;
    g_motion.center_cal.last_reject_gate_mask = gate_mask;
    g_motion.center_cal.last_reject_ms = PatrolRuntimePort_Now();
}

static uint8_t center_feedback_ready(const PatrolDiagnosticsSnapshot* s,
                                     uint8_t require_steering_disabled)
{
    return CenterCalibration_GateMask(s, require_steering_disabled) == 0U ? 1U : 0U;
}

void CenterCalibration_UpdateRelative(const PatrolDiagnosticsSnapshot* s)
{
    if (s == NULL || g_motion.center_cal.phase == CENTER_CAL_IDLE ||
        g_motion.center_cal.phase == CENTER_CAL_COMPLETE || g_motion.center_cal.phase == CENTER_CAL_FAILED ||
        g_motion.center_cal.phase == CENTER_CAL_EXPIRED) return;
    if (s->encoder_count >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS) {
        g_motion.center_cal.relative_valid = 0U;
        return;
    }
    if (s->encoder_sample_ms != 0U && s->encoder_sample_ms != g_motion.center_cal.last_encoder_sample_ms) {
        /* A larger one-sample jump cannot be produced by the bounded 100 rpm
         * commissioning jog and is treated as an encoder discontinuity. */
        if (!SteeringAngle_AccumulateSingleTurn(g_motion.center_cal.last_encoder_count,
                s->encoder_count, VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS, 256U,
                &g_motion.center_cal.encoder_delta_count))
            g_motion.center_cal.relative_valid = 0U;
        g_motion.center_cal.last_encoder_count = s->encoder_count;
        g_motion.center_cal.last_encoder_sample_ms = s->encoder_sample_ms;
    }
    g_motion.center_cal.driver_relative_mdeg = SteeringAngle_FromControllerCount(
        s->steering_position_count, g_motion.center_cal.start_driver_count,
        VEHICLE_STEERING_COUNTS_MILLI, VEHICLE_STEERING_DIRECTION);
    g_motion.center_cal.encoder_relative_mdeg = SteeringAngle_FromControllerCount(
        g_motion.center_cal.encoder_delta_count, 0, VEHICLE_ABSOLUTE_COUNTS_MILLI,
        VEHICLE_ABSOLUTE_DIRECTION);
    if (llabs((int64_t)g_motion.center_cal.driver_relative_mdeg -
              g_motion.center_cal.encoder_relative_mdeg) > VEHICLE_STEERING_TOLERANCE_MDEG)
        g_motion.center_cal.relative_valid = 0U;
}

void CenterCalibration_CaptureFailureLocked(CenterCalibrationReason reason, uint32_t gate_mask,
                                          uint32_t now,
                                          const PatrolDiagnosticsSnapshot* snapshot)
{
    if (g_motion.center_cal.failure.reason != CENTER_CAL_REASON_NONE) return;
    PatrolRuntime_CaptureFault(2U, (uint32_t)reason, gate_mask);
    g_motion.center_cal.failure.phase = (uint8_t)g_motion.center_cal.phase;
    g_motion.center_cal.failure.reason = (uint8_t)reason;
    g_motion.center_cal.failure.at_ms = now;
    g_motion.center_cal.failure.gate_mask = gate_mask;
    g_motion.center_cal.failure.bench_phase = (uint8_t)g_motion.bench.phase;
    g_motion.center_cal.failure.bench_step = g_motion.bench.step;
    g_motion.center_cal.failure.bench_fault = g_motion.bench.fault;
    g_motion.center_cal.failure.encoder_write_state = g_motion.center_cal.encoder_write_state;
    if (snapshot != NULL) {
        uint64_t encoder_errors = (uint64_t)snapshot->encoder_timeout_count +
            snapshot->encoder_crc_error_count + snapshot->encoder_exception_count +
            snapshot->encoder_format_error_count + snapshot->encoder_transport_error_count;
        g_motion.center_cal.failure.driver_raw = snapshot->steering_position_count;
        g_motion.center_cal.failure.encoder_raw = snapshot->encoder_count;
        g_motion.center_cal.failure.left_rpm = snapshot->left_actual_rpm;
        g_motion.center_cal.failure.right_rpm = snapshot->right_actual_rpm;
        g_motion.center_cal.failure.feedback_age_ms = snapshot->coherent_feedback_ms == 0U ?
            UINT32_MAX : now - snapshot->coherent_feedback_ms;
        g_motion.center_cal.failure.encoder_age_ms = snapshot->encoder_sample_ms == 0U ?
            UINT32_MAX : now - snapshot->encoder_sample_ms;
        g_motion.center_cal.failure.encoder_error_total = encoder_errors > UINT32_MAX ?
            UINT32_MAX : (uint32_t)encoder_errors;
        g_motion.center_cal.failure.encoder_error = snapshot->last_encoder_error;
        g_motion.center_cal.failure.steering_status_word = snapshot->steering_status_word;
        g_motion.center_cal.failure.drive_status_word = snapshot->drive_status_word;
        g_motion.center_cal.failure.last_sdo_abort = snapshot->last_sdo_abort_code;
    }
}

void CenterCalibration_Fail(uint32_t fault, CenterCalibrationReason reason,
                                    const PatrolDiagnosticsSnapshot* snapshot,
                                    uint32_t gate_mask)
{
    uint32_t now = PatrolRuntimePort_Now();
    PatrolRuntimePort_Enter();
    CenterCalibration_CaptureFailureLocked(reason, gate_mask, now, snapshot);
    g_motion.center_cal.phase = CENTER_CAL_FAILED;
    g_motion.center_cal.fault = fault;
    g_motion.center_cal.encoder_write_state = 0U;
    if (g_motion.bench.phase != BENCH_IDLE)
        BenchSteering_Stop(&g_motion.bench, PatrolRuntimePort_Now(), 1U);
    SteeringCoordinate_Reset(&g_motion.coordinate);
    PatrolRuntimePort_Exit();
    RemoteService_ExecutionFault();
    (void)VehiclePowerOutput_Set(0U);
    APP_LOGE("CENTER_CAL", "fail phase=%u fault=%lu reason=%s gate=%lu bench=%u/%u/%lu enc=%u",
        (unsigned int)g_motion.center_cal.failure.phase, (unsigned long)fault,
        center_reason_name(g_motion.center_cal.failure.reason), (unsigned long)gate_mask,
        (unsigned int)g_motion.center_cal.failure.bench_phase,
        (unsigned int)g_motion.center_cal.failure.bench_step,
        (unsigned long)g_motion.center_cal.failure.bench_fault,
        (unsigned int)g_motion.center_cal.failure.encoder_write_state);
}

void CenterCalibration_RequestStopForFault(CenterCalibrationReason reason, uint32_t gate_mask,
                                          uint32_t now)
{
    PatrolRuntimePort_Enter();
    if ((g_motion.center_cal.phase == CENTER_CAL_JOGGING ||
         g_motion.center_cal.phase == CENTER_CAL_STOPPING) &&
        !g_motion.center_cal.stop_fault_pending) {
        g_motion.center_cal.stop_fault_pending = 1U;
        g_motion.center_cal.stop_fault_reason = (uint8_t)reason;
        g_motion.center_cal.stop_gate_mask = gate_mask;
        BenchSteering_Stop(&g_motion.bench, now, 0U);
        g_motion.center_cal.phase = CENTER_CAL_STOPPING;
        g_motion.center_cal.deadline_ms = now + 2000U;
    }
    PatrolRuntimePort_Exit();
}

static uint8_t center_calibration_expire_idle(uint32_t now)
{
    uint8_t expired = 0U;
    PatrolRuntimePort_Enter();
    if (g_motion.center_cal.phase == CENTER_CAL_READY && g_motion.bench.phase == BENCH_IDLE &&
        (int32_t)(now - g_motion.center_cal.deadline_ms) >= 0) {
        /* No motion was requested. Expiry is a session outcome, not a device fault. */
        center_reject_locked(CENTER_CAL_REASON_READY_LEASE_TIMEOUT, 0U);
        g_motion.center_cal.phase = CENTER_CAL_EXPIRED;
        g_motion.center_cal.session_id[0] = '\0';
        g_motion.center_cal.fault = 0U;
        g_motion.center_cal.encoder_write_state = 0U;
        SteeringCoordinate_Reset(&g_motion.coordinate);
        expired = 1U;
    }
    PatrolRuntimePort_Exit();
    if (expired) {
        RemoteService_Revoke();
        (void)VehiclePowerOutput_Set(0U);
        APP_LOGW("CENTER_CAL", "idle lease expired");
    }
    return expired;
}

uint8_t CenterCalibration_Start(void)
{
    PatrolDiagnosticsSnapshot s;
    uint32_t now = PatrolRuntimePort_Now();
    uint32_t gate_mask;
    PatrolRuntime_GetDiagnostics(&s);
    gate_mask = CenterCalibration_GateMask(&s, 1U);
    PatrolRuntimePort_Enter();
    if (g_motion.bench.phase != BENCH_IDLE) gate_mask |= CENTER_GATE_BENCH_EXECUTOR_BUSY;
    if (g_motion.drive.phase != BENCH_IDLE) gate_mask |= CENTER_GATE_DRIVE_EXECUTOR_BUSY;
    if (s.encoder_count >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS)
        gate_mask |= CENTER_GATE_ENCODER_OUT_OF_RANGE;
    if ((g_motion.center_cal.phase != CENTER_CAL_IDLE &&
         g_motion.center_cal.phase != CENTER_CAL_EXPIRED &&
         g_motion.center_cal.phase != CENTER_CAL_COMPLETE) || gate_mask != 0U) {
        CenterCalibrationReason reason = CENTER_CAL_REASON_GATE_NOT_READY;
        if (g_motion.center_cal.phase != CENTER_CAL_IDLE &&
            g_motion.center_cal.phase != CENTER_CAL_EXPIRED &&
            g_motion.center_cal.phase != CENTER_CAL_COMPLETE)
            reason = CENTER_CAL_REASON_ALREADY_ACTIVE;
        else if (gate_mask & CENTER_GATE_ENCODER_OUT_OF_RANGE)
            reason = CENTER_CAL_REASON_ENCODER_OUT_OF_RANGE;
        else if (gate_mask & (CENTER_GATE_BENCH_EXECUTOR_BUSY | CENTER_GATE_DRIVE_EXECUTOR_BUSY))
            reason = CENTER_CAL_REASON_EXECUTOR_BUSY;
        center_reject_locked(reason, gate_mask);
        PatrolRuntimePort_Exit();
        return 0U;
    }
    PatrolRuntime_BeginFaultEpisode();
    memset(&g_motion.center_cal, 0, sizeof(g_motion.center_cal));
    /* Commissioning owns its captured relative origin.  Do not retain or
     * later acquire the ordinary driving coordinate using an old calibration. */
    SteeringCoordinate_Reset(&g_motion.coordinate);
    g_motion.bench.limits_aligned = 0U;
    g_motion.center_cal.serial = ++g_motion.center_cal_serial;
    g_motion.center_cal.phase = CENTER_CAL_START_STABLE;
    g_motion.center_cal.start_driver_count = s.steering_position_count;
    g_motion.center_cal.last_encoder_count = s.encoder_count;
    g_motion.center_cal.last_encoder_sample_ms = s.encoder_sample_ms;
    g_motion.center_cal.relative_valid = 1U;
    g_motion.center_cal.start_mdeg = 0;
    g_motion.center_cal.target_mdeg = 0;
    g_motion.center_cal.deadline_ms = now + 7000U;
    (void)snprintf(g_motion.center_cal.session_id, sizeof(g_motion.center_cal.session_id), "c-%lu-%lu",
                   (unsigned long)now, (unsigned long)g_motion.center_cal.serial);
    PatrolRuntimePort_Exit();
    return 1U;
}

static uint8_t center_session_matches(const char* session_id)
{
    return session_id != NULL && session_id[0] != '\0' &&
        strcmp(session_id, g_motion.center_cal.session_id) == 0;
}

uint8_t CenterCalibration_KeepAlive(const char* session_id)
{
    uint8_t accepted;
    PatrolRuntimePort_Enter();
    accepted = g_motion.center_cal.phase != CENTER_CAL_IDLE &&
        g_motion.center_cal.phase != CENTER_CAL_EXPIRED && center_session_matches(session_id);
    if (!accepted)
        center_reject_locked(g_motion.center_cal.phase == CENTER_CAL_IDLE ?
            CENTER_CAL_REASON_WRONG_PHASE : CENTER_CAL_REASON_SESSION_MISMATCH, 0U);
    else {
        /* A read/keepalive is not a successful retry of the rejected command. */
        /* Reading status never extends the READY idle deadline. */
    }
    PatrolRuntimePort_Exit();
    return accepted;
}

uint8_t CenterCalibration_Jog(const char* session_id, int16_t delta_mdeg)
{
    PatrolDiagnosticsSnapshot s;
    int32_t next;
    uint32_t gate_mask;
    uint8_t valid_delta;
    PatrolRuntime_GetDiagnostics(&s);
    gate_mask = CenterCalibration_GateMask(&s, 0U);
    valid_delta = (uint8_t)(delta_mdeg == VEHICLE_CENTER_JOG_FINE_MDEG ||
        delta_mdeg == -VEHICLE_CENTER_JOG_FINE_MDEG ||
        delta_mdeg == VEHICLE_CENTER_JOG_COARSE_MDEG ||
        delta_mdeg == -VEHICLE_CENTER_JOG_COARSE_MDEG ||
        delta_mdeg == VEHICLE_CENTER_JOG_LARGE_MDEG ||
        delta_mdeg == -VEHICLE_CENTER_JOG_LARGE_MDEG);
    PatrolRuntimePort_Enter();
    next = g_motion.center_cal.accumulated_mdeg + delta_mdeg;
    if (g_motion.center_cal.phase != CENTER_CAL_READY || !center_session_matches(session_id) ||
        !valid_delta || labs(next) > VEHICLE_CENTER_COMMISSIONING_LIMIT_MDEG ||
        !g_motion.center_cal.relative_valid || gate_mask != 0U) {
        CenterCalibrationReason reason = CENTER_CAL_REASON_GATE_NOT_READY;
        if (g_motion.center_cal.phase != CENTER_CAL_READY) reason = CENTER_CAL_REASON_WRONG_PHASE;
        else if (!center_session_matches(session_id)) reason = CENTER_CAL_REASON_SESSION_MISMATCH;
        else if (!valid_delta) reason = CENTER_CAL_REASON_INVALID_JOG_DELTA;
        else if (labs(next) > VEHICLE_CENTER_COMMISSIONING_LIMIT_MDEG)
            reason = CENTER_CAL_REASON_JOG_LIMIT;
        else if (!g_motion.center_cal.relative_valid) reason = CENTER_CAL_REASON_RELATIVE_MISMATCH;
        center_reject_locked(reason, gate_mask);
        PatrolRuntimePort_Exit();
        return 0U;
    }
    g_motion.center_cal.last_reject_reason = CENTER_CAL_REASON_NONE;
    g_motion.center_cal.accumulated_mdeg = next;
    g_motion.center_cal.target_mdeg = g_motion.center_cal.start_mdeg + next;
    g_motion.center_cal.stable_samples = 0U;
    g_motion.center_cal.last_sample_ms = 0U;
    g_motion.center_cal.deadline_ms = PatrolRuntimePort_Now() + 7000U;
    g_motion.center_cal.phase = CENTER_CAL_JOGGING;
    PatrolRuntimePort_Exit();
    return 1U;
}

uint8_t CenterCalibration_Commit(const char* session_id)
{
    PatrolDiagnosticsSnapshot s;
    uint32_t gate_mask;
    PatrolRuntime_GetDiagnostics(&s);
    gate_mask = CenterCalibration_GateMask(&s, 1U);
    PatrolRuntimePort_Enter();
    if (g_motion.center_cal.phase != CENTER_CAL_READY || !center_session_matches(session_id) ||
        g_motion.bench.phase != BENCH_IDLE || gate_mask != 0U) {
        CenterCalibrationReason reason = CENTER_CAL_REASON_GATE_NOT_READY;
        if (g_motion.center_cal.phase != CENTER_CAL_READY) reason = CENTER_CAL_REASON_WRONG_PHASE;
        else if (!center_session_matches(session_id)) reason = CENTER_CAL_REASON_SESSION_MISMATCH;
        else if (g_motion.bench.phase != BENCH_IDLE) reason = CENTER_CAL_REASON_COMMIT_BENCH_BUSY;
        else if (gate_mask & CENTER_GATE_STEERING_OPERATION_ENABLED)
            reason = CENTER_CAL_REASON_COMMIT_STEERING_ENABLED;
        center_reject_locked(reason, gate_mask);
        PatrolRuntimePort_Exit();
        return 0U;
    }
    g_motion.center_cal.last_reject_reason = CENTER_CAL_REASON_NONE;
    g_motion.center_cal.phase = CENTER_CAL_COMMIT_STABLE;
    g_motion.center_cal.stable_samples = 0U;
    g_motion.center_cal.last_sample_ms = 0U;
    g_motion.center_cal.deadline_ms = PatrolRuntimePort_Now() + 7000U;
    PatrolRuntimePort_Exit();
    return 1U;
}

uint8_t CenterCalibration_Cancel(const char* session_id)
{
    PatrolRuntimePort_Enter();
    if (g_motion.center_cal.phase == CENTER_CAL_IDLE || !center_session_matches(session_id)) {
        center_reject_locked(g_motion.center_cal.phase == CENTER_CAL_IDLE ?
            CENTER_CAL_REASON_WRONG_PHASE : CENTER_CAL_REASON_SESSION_MISMATCH, 0U);
        PatrolRuntimePort_Exit();
        return 0U;
    }
    if (g_motion.bench.phase != BENCH_IDLE) {
        BenchSteering_Stop(&g_motion.bench, PatrolRuntimePort_Now(), 0U);
        g_motion.center_cal.phase = CENTER_CAL_STOPPING;
        g_motion.center_cal.cancel_requested = 1U;
        g_motion.center_cal.deadline_ms = PatrolRuntimePort_Now() + 2000U;
        PatrolRuntimePort_Exit();
        return 1U;
    }
    memset(&g_motion.center_cal, 0, sizeof(g_motion.center_cal));
    PatrolRuntimePort_Exit();
    RemoteService_Revoke();
    (void)VehiclePowerOutput_Set(0U);
    return 1U;
}

uint8_t CenterCalibration_FormatDiagnostics(char* out, uint16_t size)
{
    PatrolDiagnosticsSnapshot s;
    const ChassisControl* c = RemoteService_State();
    int length;
    PatrolRuntime_GetDiagnostics(&s);
    PatrolRuntimePort_Enter();
    length = snprintf(out, size,
        "{\"phase\":%u,\"sessionId\":\"%s\",\"detailReason\":\"%s\",\"fault\":%lu,\"driverRaw\":%ld,"
        "\"encoderRaw\":%lu,\"targetMdeg\":%ld,\"jogOffsetMdeg\":%ld,"
        "\"profileReady\":%s,\"rangeValid\":%s,\"calibrated\":%s,"
        "\"driverRelativeMdeg\":%ld,\"encoderRelativeMdeg\":%ld,"
        "\"relativeValid\":%s,\"commissioningLimitMdeg\":%ld,\"diagVersion\":1,"
        "\"failure\":{\"phase\":%u,\"reason\":\"%s\",\"atMs\":%lu,\"gateMask\":%lu,"
        "\"driverRaw\":%ld,\"encoderRaw\":%lu,\"leftRpm\":%ld,\"rightRpm\":%ld,"
        "\"feedbackAge\":%lu,\"encoderAge\":%lu,\"encoderErrTotal\":%lu,"
        "\"steeringExecutorPhase\":%u,\"steeringExecutorStep\":%u,\"steeringExecutorFault\":%lu,"
        "\"encoderWrite\":%u,\"encoderError\":%u,\"steerSw\":%u,"
        "\"driveSw\":%u,\"lastAbort\":%lu},"
        "\"lastReject\":{\"reason\":\"%s\",\"atMs\":%lu,\"gateMask\":%lu}}",
        (unsigned)g_motion.center_cal.phase, g_motion.center_cal.session_id,
        center_reason_name(g_motion.center_cal.failure.reason != CENTER_CAL_REASON_NONE ?
            g_motion.center_cal.failure.reason : g_motion.center_cal.last_reject_reason),
        (unsigned long)g_motion.center_cal.fault,
        (long)s.steering_position_count, (unsigned long)s.encoder_count,
        (long)g_motion.center_cal.target_mdeg, (long)g_motion.center_cal.accumulated_mdeg,
        PatrolConfig_SingleTurnProfileReady() ? "true" : "false",
        c->feedback.encoder_range_valid ? "true" : "false",
        PatrolConfig_Get()->calibrated ? "true" : "false",
        (long)g_motion.center_cal.driver_relative_mdeg, (long)g_motion.center_cal.encoder_relative_mdeg,
        g_motion.center_cal.relative_valid ? "true" : "false",
        (long)VEHICLE_CENTER_COMMISSIONING_LIMIT_MDEG,
        (unsigned int)g_motion.center_cal.failure.phase,
        center_reason_name(g_motion.center_cal.failure.reason),
        (unsigned long)g_motion.center_cal.failure.at_ms,
        (unsigned long)g_motion.center_cal.failure.gate_mask,
        (long)g_motion.center_cal.failure.driver_raw,
        (unsigned long)g_motion.center_cal.failure.encoder_raw,
        (long)g_motion.center_cal.failure.left_rpm,
        (long)g_motion.center_cal.failure.right_rpm,
        (unsigned long)g_motion.center_cal.failure.feedback_age_ms,
        (unsigned long)g_motion.center_cal.failure.encoder_age_ms,
        (unsigned long)g_motion.center_cal.failure.encoder_error_total,
        (unsigned int)g_motion.center_cal.failure.bench_phase,
        (unsigned int)g_motion.center_cal.failure.bench_step,
        (unsigned long)g_motion.center_cal.failure.bench_fault,
        (unsigned int)g_motion.center_cal.failure.encoder_write_state,
        (unsigned int)g_motion.center_cal.failure.encoder_error,
        (unsigned int)g_motion.center_cal.failure.steering_status_word,
        (unsigned int)g_motion.center_cal.failure.drive_status_word,
        (unsigned long)g_motion.center_cal.failure.last_sdo_abort,
        center_reason_name(g_motion.center_cal.last_reject_reason),
        (unsigned long)g_motion.center_cal.last_reject_ms,
        (unsigned long)g_motion.center_cal.last_reject_gate_mask);
    PatrolRuntimePort_Exit();
    return (uint8_t)(length >= 0 && length < size);
}

uint8_t CenterCalibration_Format(char* out, uint16_t size)
{
    const CenterCalibration* cal = &g_motion.center_cal;
    const char* state = "IDLE";
    const char* reason = "NONE";
    int length;
    PatrolRuntimePort_Enter();
    if (cal->phase == CENTER_CAL_READY) state = "READY";
    else if (cal->phase == CENTER_CAL_COMPLETE) state = "DONE";
    else if (cal->phase == CENTER_CAL_FAILED) state = "ERROR";
    else if (CenterCalibration_IsActive()) state = "BUSY";
    if (cal->phase == CENTER_CAL_EXPIRED) reason = "EXPIRED";
    else if (cal->phase == CENTER_CAL_FAILED) reason = center_business_reason(cal->failure.reason);
    else if (cal->last_reject_reason != CENTER_CAL_REASON_NONE)
        reason = center_business_reason(cal->last_reject_reason);
    length = snprintf(out, size,
        "{\"state\":\"%s\",\"sessionId\":\"%s\",\"calibrated\":%s,"
        "\"reason\":\"%s\",\"jogOffsetMdeg\":%ld}",
        state, cal->session_id, PatrolConfig_SingleTurnProfileReady() ? "true" : "false",
        reason, (long)cal->accumulated_mdeg);
    PatrolRuntimePort_Exit();
    return (uint8_t)(length >= 0 && length < size);
}

void CenterCalibration_Tick(uint32_t now)
{
    PatrolDiagnosticsSnapshot s;
    CenterCalibrationPhase phase = g_motion.center_cal.phase;
    uint32_t gate_mask;
    now = PatrolRuntimePort_Now();
    if (phase == CENTER_CAL_COMPLETE) {
        if ((int32_t)(now - g_motion.center_cal.deadline_ms) >= 0) {
            PatrolRuntimePort_Enter();
            if (g_motion.center_cal.phase == CENTER_CAL_COMPLETE)
                memset(&g_motion.center_cal, 0, sizeof(g_motion.center_cal));
            PatrolRuntimePort_Exit();
        }
        return;
    }
    if (phase == CENTER_CAL_IDLE || phase == CENTER_CAL_FAILED || phase == CENTER_CAL_EXPIRED) return;
    PatrolRuntime_GetDiagnostics(&s);
    /* Never subtract a newer RX sample from an older task timestamp. */
    gate_mask = CenterCalibration_GateMask(&s,
        (uint8_t)(phase == CENTER_CAL_START_STABLE || phase == CENTER_CAL_COMMIT_STABLE));
    /* READY is idle, not immune to a terminal fault.  Keep transient stale
     * samples as motion gates, but never advertise READY after power/fault loss. */
    if (g_motion.bench.phase == BENCH_FAULT || g_motion.drive.phase == BENCH_FAULT) {
        CenterCalibration_Fail(2U, CENTER_CAL_REASON_BENCH_EXECUTOR_FAULT, &s, gate_mask);
        return;
    }
    if (gate_mask & (CENTER_GATE_POWER_OFF | CENTER_GATE_CAN_BUS_OFF |
                    CENTER_GATE_CAN_BUS_OFF_ACTIVE | CENTER_GATE_OTA_BUSY)) {
        CenterCalibration_Fail(2U, CENTER_CAL_REASON_GATE_NOT_READY, &s, gate_mask);
        return;
    }
    if (phase == CENTER_CAL_READY && !g_motion.center_cal.relative_valid) {
        CenterCalibration_Fail(2U, CENTER_CAL_REASON_RELATIVE_MISMATCH, &s, gate_mask);
        return;
    }
    if ((int32_t)(now - g_motion.center_cal.deadline_ms) >= 0) {
        if (phase == CENTER_CAL_READY && center_calibration_expire_idle(now)) return;
        CenterCalibration_Fail(1U, CENTER_CAL_REASON_PHASE_TIMEOUT, &s, gate_mask);
        return;
    }
    if (phase == CENTER_CAL_START_STABLE) {
        if (!g_motion.center_cal.relative_valid) {
            CenterCalibration_Fail(2U, CENTER_CAL_REASON_RELATIVE_MISMATCH, &s, gate_mask);
            return;
        }
        if (!center_feedback_ready(&s, 1U)) {
            CenterCalibration_Fail(2U, CENTER_CAL_REASON_GATE_NOT_READY, &s, gate_mask);
            return;
        }
        if (s.coherent_feedback_ms == 0U || s.coherent_feedback_ms == g_motion.center_cal.last_sample_ms) return;
        g_motion.center_cal.last_sample_ms = s.coherent_feedback_ms;
        if (++g_motion.center_cal.stable_samples < 3U) return;
        PatrolRuntimePort_Enter();
        g_motion.center_cal.phase = CENTER_CAL_READY;
        g_motion.center_cal.deadline_ms = now + CENTER_CAL_READY_IDLE_MS;
        PatrolRuntimePort_Exit();
        return;
    }
    if (phase == CENTER_CAL_JOGGING || phase == CENTER_CAL_STOPPING) {
        if (g_motion.bench.phase == BENCH_FAULT) {
            CenterCalibration_Fail(2U, CENTER_CAL_REASON_BENCH_EXECUTOR_FAULT, &s, gate_mask);
            return;
        }
        if (!g_motion.center_cal.relative_valid || !center_feedback_ready(&s, 0U))
            CenterCalibration_RequestStopForFault(
                g_motion.center_cal.relative_valid ? CENTER_CAL_REASON_GATE_NOT_READY :
                CENTER_CAL_REASON_RELATIVE_MISMATCH, gate_mask, now);
        if (phase == CENTER_CAL_STOPPING && g_motion.bench.phase == BENCH_IDLE) {
            uint8_t cancelled = 0U;
            if (g_motion.center_cal.stop_fault_pending) {
                CenterCalibration_Fail(2U,
                    (CenterCalibrationReason)g_motion.center_cal.stop_fault_reason, &s,
                    g_motion.center_cal.stop_gate_mask);
                return;
            }
            PatrolRuntimePort_Enter();
            if (g_motion.center_cal.cancel_requested) {
                memset(&g_motion.center_cal, 0, sizeof(g_motion.center_cal));
                cancelled = 1U;
            } else {
                g_motion.center_cal.phase = CENTER_CAL_READY;
                g_motion.center_cal.deadline_ms = now + CENTER_CAL_READY_IDLE_MS;
            }
            PatrolRuntimePort_Exit();
            if (cancelled) {
                RemoteService_Revoke();
                (void)VehiclePowerOutput_Set(0U);
            }
        }
        return;
    }
    if (phase == CENTER_CAL_COMMIT_STABLE) {
        if (g_motion.bench.phase != BENCH_IDLE) {
            CenterCalibration_Fail(3U, CENTER_CAL_REASON_COMMIT_BENCH_BUSY, &s, gate_mask);
            return;
        }
        if ((s.steering_status_word & 0x006FU) == 0x0027U) {
            CenterCalibration_Fail(3U, CENTER_CAL_REASON_COMMIT_STEERING_ENABLED, &s, gate_mask);
            return;
        }
        if (!center_feedback_ready(&s, 1U)) {
            CenterCalibration_Fail(3U, CENTER_CAL_REASON_GATE_NOT_READY, &s, gate_mask);
            return;
        }
        if (s.coherent_feedback_ms == 0U || s.coherent_feedback_ms == g_motion.center_cal.last_sample_ms) return;
        g_motion.center_cal.last_sample_ms = s.coherent_feedback_ms;
        if (++g_motion.center_cal.stable_samples < 3U) return;
        g_motion.center_cal.captured_driver_count = s.steering_position_count;
        if (!PatrolConfig_ClearCalibration()) {
            CenterCalibration_Fail(4U, CENTER_CAL_REASON_CONFIG_CLEAR_FAILED, &s, gate_mask);
            return;
        }
        PatrolRuntimePort_Enter();
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.center_cal.encoder_write_state = 1U;
        g_motion.center_cal.phase = CENTER_CAL_ENCODER_WRITE;
        g_motion.center_cal.deadline_ms = now + 2000U;
        PatrolRuntimePort_Exit();
        return;
    }
    if (phase == CENTER_CAL_ENCODER_WRITE) {
        if (g_motion.center_cal.encoder_write_state == 3U) {
            CenterCalibration_Fail(5U, CENTER_CAL_REASON_ENCODER_TX_FAILED, &s, gate_mask);
            return;
        }
        if (g_motion.center_cal.encoder_write_state == 2U || g_motion.center_cal.encoder_write_state == 5U) {
            PatrolRuntimePort_Enter();
            g_motion.center_cal.phase = CENTER_CAL_ENCODER_VERIFY;
            g_motion.center_cal.stable_samples = 0U;
            g_motion.center_cal.last_sample_ms = s.encoder_sample_ms;
            g_motion.center_cal.deadline_ms = now + 2000U;
            PatrolRuntimePort_Exit();
        }
        return;
    }
    if (phase == CENTER_CAL_ENCODER_VERIFY) {
        if (s.encoder_sample_ms == 0U || s.encoder_sample_ms == g_motion.center_cal.last_sample_ms) return;
        g_motion.center_cal.last_sample_ms = s.encoder_sample_ms;
        if (s.encoder_count >= VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS ||
            (s.encoder_count > 2U && s.encoder_count < VEHICLE_ABSOLUTE_SINGLE_TURN_COUNTS - 2U)) {
            CenterCalibration_Fail(6U, CENTER_CAL_REASON_ENCODER_VERIFY_MISMATCH, &s, gate_mask);
            return;
        }
        if (++g_motion.center_cal.stable_samples < 3U) return;
        if (!PatrolConfig_SetCalibration(g_motion.center_cal.captured_driver_count,
                VEHICLE_ABSOLUTE_CENTER_COUNT, VEHICLE_STEERING_COUNTS_MILLI,
                VEHICLE_ABSOLUTE_COUNTS_MILLI, VEHICLE_STEERING_DIRECTION,
                VEHICLE_ABSOLUTE_DIRECTION, VEHICLE_STEERING_TOLERANCE_MDEG)) {
            CenterCalibration_Fail(7U, CENTER_CAL_REASON_CONFIG_SAVE_FAILED, &s, gate_mask);
            return;
        }
        RemoteService_Revoke();
        (void)VehiclePowerOutput_Set(0U);
        PatrolRuntimePort_Enter();
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.center_cal.phase = CENTER_CAL_COMPLETE;
        g_motion.center_cal.deadline_ms = now + CENTER_CAL_DONE_OBSERVE_MS;
        PatrolRuntimePort_Exit();
    }
}
