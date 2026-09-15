#include "motion_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_log.h"
#include "remote_service.h"
#include "vcu_config.h"
#include "vehicle_power_output.h"
#include "vehicle_steering_profile.h"

PatrolMotion g_motion;

#ifndef PATROL_CHASSIS_BENCH_STEERING_ENABLED
#define PATROL_CHASSIS_BENCH_STEERING_ENABLED 0
#endif
#ifndef PATROL_CHASSIS_BENCH_DRIVE_ENABLED
#define PATROL_CHASSIS_BENCH_DRIVE_ENABLED 0
#endif

static MotionFaultRecord last_fault;
static uint8_t fault_captured, fault_dirty, fault_storage;
uint8_t PatrolRuntime_GetFault(MotionFaultRecord* record)
{
    PatrolRuntimePort_Enter(); *record = last_fault; PatrolRuntimePort_Exit();
    return record->magic == 0x4D463031U;
}
void PatrolRuntime_BeginFaultEpisode(void) { if (!fault_dirty) fault_captured=0; }
uint8_t PatrolRuntime_FaultStorage(void) { return fault_storage; }
void PatrolRuntime_CaptureFault(uint32_t source, uint32_t reason, uint32_t gates)
{
    PatrolRuntimePort_Enter();
    if (!fault_captured) {
        uint32_t now=PatrolRuntimePort_Now(), sequence=last_fault.sequence+1U;
        const ChassisControl* c=RemoteService_State();
        PatrolDiagnostics* d=&g_motion.diagnostics;
        memset(&last_fault,0,sizeof(last_fault));
        last_fault.magic=0x4D463031U; last_fault.sequence=sequence;
        last_fault.at_ms=now; last_fault.source=source; last_fault.reason=reason; last_fault.gates=gates;
        last_fault.target_rpm=g_motion.drive.phase != BENCH_IDLE ? g_motion.drive.writing : c->target_rpm; last_fault.target_mdeg=c->target_steering_mdeg;
        last_fault.left_rpm=d->left_actual_rpm; last_fault.right_rpm=d->right_actual_rpm;
        last_fault.driver_raw=d->steering_position_count; last_fault.encoder_raw=d->encoder_count;
        last_fault.steer_sw=d->steering_status_word; last_fault.drive_sw=d->drive_status_word;
        last_fault.left_code=(uint32_t)d->drive_objects[0]; last_fault.right_code=(uint32_t)d->drive_objects[1];
        last_fault.left_code_age=d->drive_code_ms[0] ? now-d->drive_code_ms[0] : UINT32_MAX;
        last_fault.right_code_age=d->drive_code_ms[1] ? now-d->drive_code_ms[1] : UINT32_MAX;
        last_fault.steer_age=d->steering_status_ms ? now-d->steering_status_ms : UINT32_MAX;
        last_fault.drive_age=d->drive_status_ms ? now-d->drive_status_ms : UINT32_MAX;
        last_fault.bench_phase=g_motion.bench.phase; last_fault.bench_step=g_motion.bench.step;
        last_fault.bench_fault=g_motion.bench.fault; last_fault.drive_phase=g_motion.drive.phase;
        last_fault.drive_step=g_motion.drive.step; last_fault.drive_fault=g_motion.drive.fault;
        last_fault.last_abort=d->last_sdo_abort_code; last_fault.encoder_error=d->last_encoder_error;
        last_fault.center_phase=g_motion.center_cal.phase;
        fault_captured=1; fault_dirty=1; fault_storage=0;
    }
    PatrolRuntimePort_Exit();
}

void PatrolRuntime_Init(void)
{
    PatrolDiagnostics_Init(&g_motion.diagnostics);
    memset(&last_fault,0,sizeof(last_fault));
    fault_captured=0; fault_dirty=0;
    fault_storage=OTA_Info_LoadMotionFault(&last_fault) ? 1U : 0U;
}

void PatrolRuntime_GetDiagnostics(PatrolDiagnosticsSnapshot* snapshot)
{
    if (snapshot == NULL) return;
    PatrolRuntimePort_Enter();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, PatrolRuntimePort_Now(), snapshot);
    PatrolRuntimePort_Exit();
}

static uint8_t bench_arm(uint16_t limit_mdeg, uint8_t drive)
{
    PatrolDiagnosticsSnapshot s;
    uint8_t result = 0;
    PatrolRuntimePort_Enter();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, PatrolRuntimePort_Now(), &s);
    if (PATROL_CHASSIS_BENCH_STEERING_ENABLED && (!drive || PATROL_CHASSIS_BENCH_DRIVE_ENABLED) &&
        g_motion.bench.phase == BENCH_IDLE && g_motion.drive.phase == BENCH_IDLE &&
        g_motion.coordinate.valid &&
        VcuConfig_Get()->calibrated && VcuConfig_CalibrationCompatible() &&
        VcuConfig_SingleTurnProfileReady() && RemoteService_State()->feedback.encoder_range_valid &&
        s.steering_feedback_fresh && s.drive_feedback_fresh && s.encoder_online &&
        s.steering_status_fresh && s.drive_status_fresh &&
        s.encoder_config_valid && !s.drive_fault && !PatrolRuntimePort_OtaBusy() &&
        !PatrolRuntimePort_CanBusOff() && VehiclePowerOutput_IsOn() &&
        (s.steering_status_word & 0x6FU) != 0x27U && (s.drive_status_word & 4U) == 0 &&
        labs(s.left_actual_rpm) <= 20 && labs(s.right_actual_rpm) <= 20)
        result = drive ? RemoteService_BenchDriveArm(limit_mdeg, PatrolRuntimePort_Now()) :
            RemoteService_BenchArm(limit_mdeg, PatrolRuntimePort_Now());
    PatrolRuntimePort_Exit();
    if (result) PatrolRuntime_BeginFaultEpisode();
    return result;
}
uint8_t PatrolRuntime_BenchArm(uint16_t limit) { return bench_arm(limit, 0); }
uint8_t PatrolRuntime_BenchDriveArm(uint16_t limit) { return bench_arm(limit, 1); }
uint8_t PatrolRuntime_ResetFault(void)
{
    PatrolRuntimePort_Enter();
    if (VehiclePowerOutput_IsOn() || PatrolRuntimePort_OtaBusy() || fault_dirty ||
        g_motion.center_cal.encoder_write_state==4U) { PatrolRuntimePort_Exit(); return 0; }
    RemoteService_BenchRevoke(); PatrolRuntimePort_AbortCanTx();
    BenchSteering_Init(&g_motion.bench); BenchDrive_Init(&g_motion.drive);
    g_motion.diagnostics.pending_valid=0; g_motion.bench_transport_stopped=0;
    SteeringCoordinate_Reset(&g_motion.coordinate); g_motion.coordinate_reset_requested=0;
    g_motion.center_cal.phase=CENTER_CAL_IDLE; g_motion.center_cal.session_id[0]=0;
    g_motion.center_cal.encoder_write_state=0; g_motion.center_cal.fault=0;
    RemoteService_Init(); /* No permit, enable, target or previous session survives. */
    fault_captured=0;
    PatrolRuntimePort_Exit(); return 1;
}
void PatrolRuntime_BenchStop(void)
{
    PatrolRuntimePort_Enter();
    RemoteService_BenchRevoke();
    BenchSteering_Stop(&g_motion.bench, PatrolRuntimePort_Now(), 0);
    BenchDrive_Stop(&g_motion.drive, PatrolRuntimePort_Now(), 0);
    PatrolRuntimePort_Exit();
}
void PatrolRuntime_FormatBench(char* out, uint16_t size)
{
    PatrolRuntimePort_Enter();
    snprintf(out, size, "{\"available\":%s,\"permitted\":%s,\"phase\":%u,\"step\":%u,"
        "\"fault\":%lu,\"feedbackGateMask\":%u,\"coordinateValid\":%s,\"runtimeCenterCount\":%ld,"
        "\"limitMdeg\":%u,\"sessionId\":\"%s\",\"driveAvailable\":%s,\"drivePermitted\":%s,\"drivePhase\":%u,\"driveFault\":%lu,\"driveLimitRpm\":200,"
        "\"safetyProfile\":\"single_turn_4096_center0_limit30_margin05_v3\",\"profileReady\":%s,\"rangeValid\":%s,\"contactorFeedbackAvailable\":false}",
        PATROL_CHASSIS_BENCH_STEERING_ENABLED ? "true" : "false",
        RemoteService_BenchPermitted() ? "true" : "false", (unsigned)g_motion.bench.phase,
        (unsigned)g_motion.bench.step, (unsigned long)g_motion.bench.fault,
        g_motion.bench.phase == BENCH_FAULT ? g_motion.bench_fault_gate_mask : g_motion.feedback_gate_mask,
        g_motion.coordinate.valid ? "true" : "false", (long)g_motion.coordinate.runtime_center,
        RemoteService_BenchLimit(), RemoteService_BenchSession(),
        PATROL_CHASSIS_BENCH_DRIVE_ENABLED ? "true" : "false",
        RemoteService_BenchDrivePermitted() ? "true" : "false", (unsigned)g_motion.drive.phase, (unsigned long)g_motion.drive.fault,
        VcuConfig_SingleTurnProfileReady() ? "true" : "false",
        RemoteService_State()->feedback.encoder_range_valid ? "true" : "false");
    PatrolRuntimePort_Exit();
}

void PatrolRuntime_OnCanFrame(const PatrolCanFrame* frame, uint32_t now)
{
    PatrolRuntimePort_Enter();
    /* Exactly one SDO owner; executor responses never satisfy a diagnostic request. */
    if ((frame->id == 0x701 || frame->id == 0x702) && frame->dlc == 1 && frame->data[0] == 0 &&
        (g_motion.coordinate.valid || CenterCalibration_IsActive()))
        g_motion.coordinate_reset_requested = 1;
    uint8_t execution = BenchSteering_OnFrame(&g_motion.bench, frame, now) ? 1 :
        BenchDrive_OnFrame(&g_motion.drive, frame, now) ? 2 : 0;
    if (!execution)
        PatrolDiagnostics_OnCanFrame(&g_motion.diagnostics, frame, now);
    else {
        int32_t status; uint8_t width;
        g_motion.diagnostics.can_rx_count++;
        /* An executor-owned validated status upload is also fresh feedback;
         * it must not complete a diagnostics transaction or advance its cursor. */
        if (PatrolCanopen_ParseSdoValue(frame, execution, 0x6041, 0, &status, &width) && width == 2) {
            if (execution == 1) {
                g_motion.diagnostics.steering_status_word = (uint16_t)status;
                g_motion.diagnostics.steering_status_ms = now;
            } else {
                g_motion.diagnostics.drive_status_word = (uint16_t)status;
                g_motion.diagnostics.drive_status_ms = now;
            }
        }
    }
    PatrolRuntimePort_Exit();
}

void PatrolRuntime_ApplyReset(uint32_t now)
{
    if (!VehiclePowerOutput_IsOn()) {
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.bench.limits_aligned = 0;
    }
    if (g_motion.coordinate_reset_requested) {
        /* Power-off already invalidated the coordinate above.  A late
         * node Boot-up heartbeat during intentional de-energization
         * must not turn an idle executor into BENCH_FAULT.  While
         * powered, a reset of a valid coordinate remains a latched
         * safety fault. */
        if (VehiclePowerOutput_IsOn() && CenterCalibration_IsActive()) {
            PatrolDiagnosticsSnapshot reset_snapshot;
            PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &reset_snapshot);
            CenterCalibration_Fail(2U, CENTER_CAL_REASON_POSITION_EPOCH_RESET,
                &reset_snapshot, CenterCalibration_GateMask(&reset_snapshot, 0U));
        }
        if (g_motion.coordinate.valid) BenchSteering_Stop(&g_motion.bench, now, 1);
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.coordinate_reset_requested = 0;
    }
}

uint32_t PatrolRuntime_ApplyCut(uint32_t now)
{
    if (g_motion.bench.cut_power || g_motion.drive.cut_power) {
        PatrolRuntimePort_Enter();
        now = PatrolRuntimePort_Now();
        PatrolRuntime_CaptureFault(1U, (uint32_t)RemoteService_State()->stop_reason, g_motion.feedback_gate_mask);
        /* Capture before cross-stopping the other executor and before
         * power-off turns healthy samples into secondary offline gates. */
        if (CenterCalibration_IsActive()) {
            PatrolDiagnosticsSnapshot fault_snapshot;
            PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &fault_snapshot);
            CenterCalibration_CaptureFailureLocked(CENTER_CAL_REASON_BENCH_EXECUTOR_FAULT,
                CenterCalibration_GateMask(&fault_snapshot, 0U), now, &fault_snapshot);
            g_motion.center_cal.phase = CENTER_CAL_FAILED;
            g_motion.center_cal.fault = 2U;
            g_motion.center_cal.encoder_write_state = 0U;
        }
        BenchDrive_Stop(&g_motion.drive, now, 1);
        BenchSteering_Stop(&g_motion.bench, now, 1);
        g_motion.bench_fault_gate_mask = g_motion.feedback_gate_mask;
        RemoteService_BenchFault();
        (void)VehiclePowerOutput_Set(0);
        g_motion.bench.cut_power = 0; g_motion.drive.cut_power = 0;
        PatrolRuntimePort_Exit();
    }
    return now;
}

uint8_t PatrolRuntime_Take(uint32_t now, PatrolCanFrame* request, uint8_t* owner)
{
    uint8_t have_request = 0, bench_frame = 0;
    /* Alternate execution and diagnostics, never overlap pending transactions. */
    g_motion.diagnostics.stop_priority = g_motion.bench.phase == BENCH_STOPPING;
    if (!g_motion.diagnostics.pending_valid && !g_motion.bench.pending && !g_motion.drive.pending &&
        (g_motion.bench_turn || BenchSteering_StopFinishReady(&g_motion.bench))) {
        if (g_motion.drive_turn && g_motion.bench.phase != BENCH_STOPPING) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
        if (!have_request) {
            have_request = BenchSteering_Take(&g_motion.bench, now, request);
            if (have_request) { bench_frame = 1; g_motion.drive_turn = 1; }
        }
        if (!have_request) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
        if (have_request) g_motion.bench_turn = 0;
    }
    if (!have_request && !g_motion.bench.pending && !g_motion.drive.pending) {
        have_request = PatrolDiagnostics_TakeSdoRead(&g_motion.diagnostics, now, request);
        if (have_request) g_motion.bench_turn = 1;
    }
    if (!have_request && !g_motion.diagnostics.pending_valid && !g_motion.bench.pending && !g_motion.drive.pending) {
        if (g_motion.drive_turn && g_motion.bench.phase != BENCH_STOPPING) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
        if (!have_request) {
            have_request = BenchSteering_Take(&g_motion.bench, now, request);
            if (have_request) { bench_frame = 1; g_motion.drive_turn = 1; }
        }
        if (!have_request) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
    }
    *owner = bench_frame;
    return have_request;
}

static uint32_t feedback_due;

void PatrolRuntime_UpdateFeedback(uint32_t now)
{
    PatrolDiagnosticsSnapshot snapshot;
    ChassisFeedback feedback;
    const VcuRemoteConfig_t* cfg = VcuConfig_Get();
    memset(&feedback, 0, sizeof(feedback));
    PatrolRuntimePort_Enter();
    /* Read time and feedback under the same lock: RX can otherwise publish a
     * timestamp newer than now and unsigned age arithmetic reports it stale. */
    now = PatrolRuntimePort_Now();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &snapshot);
    PatrolRuntimePort_Exit();
    feedback.calibrated = cfg != NULL && cfg->calibrated && VcuConfig_CalibrationCompatible();
    feedback.encoder_profile_ready = VcuConfig_SingleTurnProfileReady();
    feedback.steering_online = snapshot.steering_online;
    feedback.drive_online = snapshot.drive_online;
    feedback.can_bus_off = PatrolRuntimePort_CanBusOff();
    feedback.drive_fault = snapshot.drive_fault;
    feedback.ota_busy = PatrolRuntimePort_OtaBusy();
    /* Bench readiness is not physical contactor feedback. No unconfirmed pin is read. */
    g_motion.feedback_gate_mask = (!snapshot.steering_feedback_fresh ? 1U : 0U) |
        (!snapshot.drive_feedback_fresh ? 2U : 0U) | (!snapshot.encoder_online ? 4U : 0U) |
        (!snapshot.steering_status_fresh ? 8U : 0U) | (!snapshot.drive_status_fresh ? 16U : 0U) |
        (!snapshot.encoder_config_valid ? 32U : 0U) |
        ((snapshot.drive_status_word & 4U) != 0 ? 64U : 0U) |
        ((labs(snapshot.left_actual_rpm) > 20 || labs(snapshot.right_actual_rpm) > 20) ? 128U : 0U) |
        (!VehiclePowerOutput_IsOn() ? 256U : 0U);
    /* Only an executing drive session may pass moving/Operation Enabled gates.
     * Keep this allowance during bounded stopping after RAM permission is revoked. */
    if (g_motion.drive.phase != BENCH_IDLE && g_motion.drive.phase != BENCH_FAULT)
        g_motion.feedback_gate_mask &= (uint16_t)~(64U | 128U);
    feedback.encoder_range_valid = SteeringAngle_AbsoluteWithinLimit(
        snapshot.encoder_count, cfg->absolute_center_count,
        cfg->absolute_counts_per_degree_milli, cfg->absolute_direction,
        VEHICLE_STEERING_TRIP_MDEG, &feedback.encoder_actual_mdeg);
    if (PATROL_CHASSIS_BENCH_STEERING_ENABLED) {
        uint8_t was_valid = g_motion.coordinate.valid;
        if (!VehiclePowerOutput_IsOn() || CenterCalibration_IsActive()) SteeringCoordinate_Reset(&g_motion.coordinate);
        else if (g_motion.coordinate.valid && SteeringCoordinate_EpochLost(
            g_motion.feedback_gate_mask, snapshot.steering_online,
            cfg->steering_center_count == g_motion.coordinate_calibration_center &&
            cfg->absolute_center_count == g_motion.coordinate_absolute_center)) {
            /* A lost position epoch requires power-off, never a live re-zero. */
            PatrolRuntimePort_Enter();
            BenchSteering_Stop(&g_motion.bench, now, 1);
            SteeringCoordinate_Reset(&g_motion.coordinate);
            PatrolRuntimePort_Exit();
        } else {
            SteeringCoordinate_Observe(&g_motion.coordinate,
                g_motion.feedback_gate_mask == 0 && feedback.calibrated && feedback.encoder_range_valid &&
                snapshot.steering_online && snapshot.drive_online &&
                !snapshot.drive_fault && !feedback.can_bus_off && !feedback.ota_busy &&
                (snapshot.steering_status_word & 0x6FU) != 0x27U,
                snapshot.steering_position_count, snapshot.encoder_count, cfg->absolute_center_count,
                snapshot.coherent_feedback_ms, now);
        }
        if (!was_valid && g_motion.coordinate.valid) {
            g_motion.coordinate_calibration_center = cfg->steering_center_count;
            g_motion.coordinate_absolute_center = cfg->absolute_center_count;
        }
        if (!g_motion.coordinate.valid) g_motion.feedback_gate_mask |= 512U;
    }
    feedback.power_ready = PATROL_CHASSIS_BENCH_STEERING_ENABLED && g_motion.feedback_gate_mask == 0;
    feedback.left_actual_rpm = snapshot.left_actual_rpm > 32767 ? 32767 :
        (snapshot.left_actual_rpm < -32768 ? -32768 : (int16_t)snapshot.left_actual_rpm);
    /* Normalize mirror-mounted right-wheel feedback to vehicle coordinates.
     * Raw controller sign remains available in get_can_diag. */
    feedback.right_actual_rpm = snapshot.right_actual_rpm > 32768 ? -32768 :
        (snapshot.right_actual_rpm < -32767 ? 32767 : (int16_t)-snapshot.right_actual_rpm);
    if (feedback.calibrated)
    {
        feedback.steering_actual_mdeg = SteeringAngle_FromControllerCount(
            snapshot.steering_position_count,
            PATROL_CHASSIS_BENCH_STEERING_ENABLED && g_motion.coordinate.valid ?
                g_motion.coordinate.runtime_center : cfg->steering_center_count,
            cfg->steering_counts_per_degree_milli, cfg->steering_direction);
        if (!feedback.encoder_range_valid)
            feedback.encoder_actual_mdeg = SteeringAngle_FromAbsoluteCount(
                snapshot.encoder_count, cfg->absolute_center_count,
                cfg->absolute_counts_per_degree_milli, cfg->absolute_direction);
    }
    feedback.feedback_ms = snapshot.coherent_feedback_ms != 0U ?
        snapshot.coherent_feedback_ms : now - (CHASSIS_FEEDBACK_TIMEOUT_MS + 1U);
    PatrolRuntimePort_Enter();
    CenterCalibration_UpdateRelative(&snapshot);
    RemoteService_UpdateFeedback(&feedback);
    if (g_motion.center_cal.phase == CENTER_CAL_JOGGING && g_motion.bench.phase == BENCH_ACTIVE &&
        snapshot.coherent_feedback_ms != 0U &&
        snapshot.coherent_feedback_ms != g_motion.center_cal.last_sample_ms &&
        g_motion.center_cal.relative_valid &&
        labs(g_motion.center_cal.driver_relative_mdeg - g_motion.center_cal.target_mdeg) <= 200L &&
        labs(g_motion.center_cal.encoder_relative_mdeg - g_motion.center_cal.target_mdeg) <= 200L) {
        g_motion.center_cal.last_sample_ms = snapshot.coherent_feedback_ms;
        if (++g_motion.center_cal.stable_samples >= 3U) {
            BenchSteering_Stop(&g_motion.bench, now, 0U);
            g_motion.center_cal.phase = CENTER_CAL_STOPPING;
            g_motion.center_cal.deadline_ms = now + 2000U;
        }
    }
    PatrolRuntimePort_Exit();
    feedback_due = now + 20U;
}

void PatrolRuntime_Tick(void)
{
    PatrolCanFrame request;
    uint8_t have_request = 0U;
    uint32_t now;
    uint8_t bench_frame = 0U;
    PatrolRuntimePort_Enter();
    now = PatrolRuntimePort_Now();
    {
        const ChassisControl* c = RemoteService_State();
        const VcuRemoteConfig_t* cfg = VcuConfig_Get();
        PatrolRuntime_ApplyReset(now);
        uint8_t normal_safe = g_motion.coordinate.valid && VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
            c->feedback.power_ready && c->feedback.calibrated &&
            c->feedback.steering_online && c->feedback.drive_online &&
            !c->feedback.drive_fault && !c->feedback.can_bus_off && !PatrolRuntimePort_OtaBusy() &&
            now - c->feedback.feedback_ms <= CHASSIS_FEEDBACK_TIMEOUT_MS &&
            llabs((int64_t)c->feedback.steering_actual_mdeg - c->feedback.encoder_actual_mdeg) <= 2000;
        uint8_t remote_requested = RemoteService_BenchPermitted() && c->session_active && c->enable_request;
        uint8_t center_requested = g_motion.center_cal.phase == CENTER_CAL_JOGGING;
        uint8_t center_executor_active = center_requested ||
            g_motion.center_cal.phase == CENTER_CAL_STOPPING;
        uint8_t center_safe = VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
            c->feedback.steering_online && c->feedback.drive_online && !c->feedback.drive_fault &&
            !c->feedback.can_bus_off && !PatrolRuntimePort_OtaBusy() && !c->fault_latched &&
            now - c->feedback.feedback_ms <= CHASSIS_FEEDBACK_TIMEOUT_MS &&
            (g_motion.feedback_gate_mask & (uint16_t)~512U) == 0U && g_motion.center_cal.relative_valid &&
            llabs((int64_t)g_motion.center_cal.driver_relative_mdeg -
                  g_motion.center_cal.encoder_relative_mdeg) <= VEHICLE_STEERING_TOLERANCE_MDEG;
        /* STOPPING still belongs to the commissioning executor.  Falling
         * back to normal_safe here rejects an intentionally uncalibrated
         * vehicle while the executor is sending its bounded stop sequence,
         * which turns a successful jog into BENCH_FAULT and cuts power. */
        uint8_t safe = center_executor_active ? center_safe : normal_safe;
        uint8_t requested = remote_requested || center_requested;
        int32_t requested_angle = center_requested ? g_motion.center_cal.target_mdeg : c->target_steering_mdeg;
        if (PATROL_CHASSIS_BENCH_STEERING_ENABLED && requested && safe && g_motion.bench.phase == BENCH_IDLE) {
            if (center_requested)
                BenchSteering_StartCommissioning(&g_motion.bench, g_motion.diagnostics.steering_position_count,
                    g_motion.center_cal.start_driver_count, VEHICLE_CENTER_COMMISSIONING_LIMIT_MDEG, now);
            else
                BenchSteering_Start(&g_motion.bench, g_motion.diagnostics.steering_position_count,
                    g_motion.coordinate.runtime_center, cfg->steering_center_count,
                    RemoteService_BenchLimit(), now);
        }
        safe = safe && !c->fault_latched;
        if (PATROL_CHASSIS_BENCH_DRIVE_ENABLED && remote_requested && safe &&
            RemoteService_BenchDrivePermitted() && g_motion.bench.phase == BENCH_ACTIVE && g_motion.drive.phase == BENCH_IDLE)
            BenchDrive_Start(&g_motion.drive, now);
        BenchDrive_Tick(&g_motion.drive, now, remote_requested && RemoteService_BenchDrivePermitted(), safe,
            c->target_rpm, g_motion.diagnostics.left_actual_rpm, g_motion.diagnostics.right_actual_rpm,
            g_motion.diagnostics.left_rpm_ms, g_motion.diagnostics.right_rpm_ms);
        BenchSteering_Tick(&g_motion.bench, now, requested && g_motion.drive.phase != BENCH_FAULT, safe,
            requested_angle, c->feedback.steering_actual_mdeg);
        g_motion.diagnostics.motion_priority = CenterCalibration_IsActive() ||
            (g_motion.bench.phase != BENCH_IDLE && g_motion.bench.phase != BENCH_FAULT) ||
            (g_motion.drive.phase != BENCH_IDLE && g_motion.drive.phase != BENCH_FAULT);
        if (g_motion.bench.phase == BENCH_STOPPING || g_motion.bench.phase == BENCH_FAULT) {
            RemoteService_BenchRevoke();
            if (!g_motion.bench_transport_stopped) {
                g_motion.diagnostics.pending_valid = 0;
                PatrolRuntimePort_AbortCanTx();
                g_motion.bench_transport_stopped = 1;
                /* Send shutdown first, then alternate confirmation and safety reads. */
                g_motion.bench_turn = 1;
            }
        } else g_motion.bench_transport_stopped = 0;
    }
    PatrolRuntimePort_Exit();
    now = PatrolRuntime_ApplyCut(now);
    /* Never write flash while powered or inside the safety critical section. */
    if (fault_dirty && !VehiclePowerOutput_IsOn() && !PatrolRuntimePort_OtaBusy()) {
        fault_storage=OTA_Info_SaveMotionFault(&last_fault) ? 1U : 2U;
        fault_dirty=0;
    }
    if (VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
        PatrolRuntimePort_CanTxReady())
    {
        PatrolRuntimePort_Enter();
        have_request = PatrolRuntime_Take(now, &request, &bench_frame);
        PatrolRuntimePort_Exit();
    }
    if (have_request)
    {
        PatrolRuntimePort_Enter();
        /* A higher priority safety tick may have revoked the selected command. */
        if (bench_frame && g_motion.center_cal.phase != CENTER_CAL_JOGGING &&
            g_motion.center_cal.phase != CENTER_CAL_STOPPING && (bench_frame == 1 ?
            (g_motion.bench.phase != BENCH_STOPPING && g_motion.bench.phase != BENCH_IDLE) :
            (g_motion.drive.phase != BENCH_STOPPING && g_motion.drive.phase != BENCH_IDLE)) &&
            (!RemoteService_BenchPermitted() || !RemoteService_State()->enable_request ||
             (int32_t)(PatrolRuntimePort_Now() - RemoteService_State()->lease_deadline_ms) >= 0)) {
            BenchSteering_Stop(&g_motion.bench, PatrolRuntimePort_Now(), 0);
            BenchDrive_Stop(&g_motion.drive, PatrolRuntimePort_Now(), 0);
            have_request = 0;
        }
        /* Each compiled executor is restricted to its own node. */
        if (have_request && ((bench_frame == 1 && PATROL_CHASSIS_BENCH_STEERING_ENABLED &&
                (request.id == 0x601U || (request.id == 0 && request.data[1] == 1))) ||
             (bench_frame == 2 && PATROL_CHASSIS_BENCH_DRIVE_ENABLED &&
                (request.id == 0x602U || (request.id == 0 && request.data[1] == 2))) ||
             (!bench_frame && (request.id == 0x601U || request.id == 0x602U) && request.data[0] == 0x40U)) &&
            !PatrolRuntimePort_SendCan(&request))
        {
            PatrolRuntimePort_Enter();
            if (bench_frame == 2) BenchDrive_TxFailed(&g_motion.drive);
            else if (bench_frame == 1) BenchSteering_TxFailed(&g_motion.bench);
            else PatrolDiagnostics_OnSdoTxError(&g_motion.diagnostics, now);
            PatrolRuntimePort_Exit();
        }
        PatrolRuntimePort_Exit();
    }
    if ((int32_t)(now - feedback_due) >= 0) {
        PatrolRuntime_UpdateFeedback(now);
    }
}

uint8_t PatrolRuntime_BeginEncoderCycle(PatrolDiagnosticsSnapshot* snapshot)
{
    uint8_t write_center = 0;
    PatrolRuntimePort_Enter();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, PatrolRuntimePort_Now(), snapshot);
    if (g_motion.center_cal.encoder_write_state == 1U) {
        g_motion.center_cal.encoder_write_state = 4U;
        write_center = 1U;
    }
    PatrolRuntimePort_Exit();
    return write_center;
}

void PatrolRuntime_EncoderWriteCompleted(uint8_t state)
{
    PatrolRuntimePort_Enter();
    g_motion.center_cal.encoder_write_state = state;
    PatrolRuntimePort_Exit();
}

void PatrolRuntime_OnEncoderConfig(const uint16_t* registers, uint32_t now)
{
    PatrolDiagnostics_OnEncoderConfig(&g_motion.diagnostics, registers, now);
}

void PatrolRuntime_OnEncoderCount(uint32_t count, uint32_t now)
{
    PatrolDiagnostics_OnEncoderCount(&g_motion.diagnostics, count, now);
}

void PatrolRuntime_OnEncoderError(PatrolEncoderError error)
{
    PatrolDiagnostics_OnEncoderError(&g_motion.diagnostics, error);
}
