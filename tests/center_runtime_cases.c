#include "center_runtime_stubs.h"

static void reset_fixture(void)
{
    memset(&g_motion.center_cal, 0, sizeof(g_motion.center_cal));
    PatrolAutoCenter_Cancel(&g_motion.auto_center);
    memset(&mock_control, 0, sizeof(mock_control));
    memset(&mock_snapshot, 0, sizeof(mock_snapshot));
    memset(&mock_config, 0, sizeof(mock_config));
    BenchSteering_Init(&g_motion.bench); BenchDrive_Init(&g_motion.drive);
    SteeringCoordinate_Reset(&g_motion.coordinate);
    g_motion.coordinate_reset_requested = mock_bus_off = mock_ota = mock_permit = 0;
    mock_cal_prepare = 0;
    g_motion.feedback_gate_mask = g_motion.bench_fault_gate_mask = 0;
    mock_can_ready = mock_revoke_before_send = mock_sent = 0;
    mock_power = 1; mock_now = 1000;
    mock_config.calibrated = 1;
    mock_config.absolute_center_count = 2048;
    mock_config.absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.absolute_direction = 1;
    mock_snapshot.steering_online = mock_snapshot.drive_online = 1;
    mock_snapshot.steering_feedback_fresh = mock_snapshot.drive_feedback_fresh = 1;
    mock_snapshot.steering_status_fresh = mock_snapshot.drive_status_fresh = 1;
    mock_snapshot.encoder_online = mock_snapshot.encoder_config_valid = 1;
    mock_snapshot.steering_status_word = mock_snapshot.drive_status_word = 0x21;
    mock_snapshot.steering_position_count = -83815;
    mock_snapshot.encoder_count = 2048;
    mock_snapshot.encoder_sample_ms = mock_snapshot.coherent_feedback_ms = mock_now;
}

static void start_ready(void)
{
    assert(CenterCalibration_Start());
    CenterCalibration_UpdateRelative(&mock_snapshot);
    for (int i = 0; i < 3; ++i) {
        mock_now += 20;
        mock_snapshot.coherent_feedback_ms = mock_now;
        CenterCalibration_Tick(mock_now);
    }
    assert(g_motion.center_cal.phase == CENTER_CAL_READY && mock_power);
}

int main(void)
{
    char json[900];
    reset_fixture();
    g_motion.coordinate.valid = 1;
    start_ready();
    assert(!g_motion.coordinate.valid); /* discard ordinary anchor on session start */
    for (int i = 0; i < 6; ++i) {
        mock_now += 100;
        mock_snapshot.coherent_feedback_ms = mock_now;
        PatrolRuntime_UpdateFeedback(mock_now);
    }
    assert(!g_motion.coordinate.valid && mock_power); /* never re-acquire during READY */
    g_motion.bench.step = 23; /* the actual executor's stop-complete state */
    mock_snapshot.steering_feedback_fresh = 0;
    mock_snapshot.drive_feedback_fresh = mock_snapshot.drive_status_fresh = 0;
    PatrolRuntime_UpdateFeedback(mock_now + 300);
    CenterCalibration_Tick(mock_now);
    PatrolRuntime_ApplyCut(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_READY && g_motion.bench.phase == BENCH_IDLE && mock_power);
    assert(!CenterCalibration_Jog(g_motion.center_cal.session_id, 5000));
    assert(g_motion.center_cal.accumulated_mdeg == 0);
    assert(g_motion.center_cal.last_reject_reason == CENTER_CAL_REASON_GATE_NOT_READY);
    assert(CenterCalibration_KeepAlive(g_motion.center_cal.session_id));
    assert(g_motion.center_cal.last_reject_reason == CENTER_CAL_REASON_GATE_NOT_READY);
    assert(CenterCalibration_Format(json, sizeof(json)));
    assert(strstr(json, "\"reason\":\"SAFETY_BLOCKED\""));
    mock_snapshot.steering_feedback_fresh = mock_snapshot.drive_feedback_fresh = mock_snapshot.drive_status_fresh = 1;
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, 5000));
    assert(g_motion.center_cal.target_mdeg == 5000 && g_motion.center_cal.phase == CENTER_CAL_JOGGING);
    mock_snapshot.steering_feedback_fresh = 0;
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_STOPPING && mock_power &&
           !mock_control.fault_latched);
    CenterCalibration_Tick(mock_now + 1);
    assert(g_motion.center_cal.phase == CENTER_CAL_FAILED && !mock_power && mock_control.fault_latched);
    assert(g_motion.center_cal.failure.reason == CENTER_CAL_REASON_GATE_NOT_READY);
    {
        char diagnostics[1100];
        assert(CenterCalibration_FormatDiagnostics(diagnostics, sizeof(diagnostics)));
        assert(strstr(diagnostics, "\"detailReason\":\"gate_not_ready\""));
    }

    /* Calibration keeps its relative two-degree guard after normal-motion
     * comparison is removed. The raw jump is valid but disagrees by ~3 deg. */
    reset_fixture(); start_ready();
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, 1000));
    mock_now += 20U;
    mock_snapshot.encoder_count += 166U;
    mock_snapshot.encoder_sample_ms = mock_snapshot.coherent_feedback_ms = mock_now;
    CenterCalibration_UpdateRelative(&mock_snapshot);
    assert(!g_motion.center_cal.relative_valid);
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_STOPPING ||
           g_motion.center_cal.phase == CENTER_CAL_FAILED);

    reset_fixture(); start_ready();
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, 1000));
    g_motion.bench.phase = BENCH_ACTIVE; g_motion.bench.step = 17;
    mock_snapshot.steering_feedback_fresh = 0;
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_STOPPING && g_motion.bench.phase == BENCH_STOPPING);
    assert(mock_power && !mock_control.fault_latched);
    g_motion.bench.phase = BENCH_IDLE;
    CenterCalibration_Tick(mock_now + 1);
    assert(g_motion.center_cal.phase == CENTER_CAL_FAILED && mock_control.fault_latched && !mock_power);

    reset_fixture(); start_ready();
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, 1000));
    g_motion.bench.phase = BENCH_ACTIVE; g_motion.bench.step = 17;
    assert(CenterCalibration_Cancel(g_motion.center_cal.session_id));
    assert(g_motion.center_cal.phase == CENTER_CAL_STOPPING && mock_power && !mock_control.fault_latched);
    g_motion.bench.phase = BENCH_IDLE;
    CenterCalibration_Tick(mock_now + 1);
    assert(g_motion.center_cal.phase == CENTER_CAL_IDLE && !mock_power && !mock_control.fault_latched);

    reset_fixture(); start_ready();
    uint32_t ready_deadline = g_motion.center_cal.deadline_ms;
    mock_now += 1000;
    assert(CenterCalibration_KeepAlive(g_motion.center_cal.session_id));
    assert(g_motion.center_cal.deadline_ms == ready_deadline);
    mock_now = g_motion.center_cal.deadline_ms;
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_EXPIRED && !mock_power && !mock_control.fault_latched);
    assert(CenterCalibration_Format(json, sizeof(json)) && strstr(json, "\"reason\":\"EXPIRED\""));
    assert(strstr(json, "\"state\":\"IDLE\"") && g_motion.center_cal.session_id[0] == '\0');

    reset_fixture(); start_ready();
    g_motion.bench.phase = BENCH_ACTIVE; g_motion.bench.step = 17;
    BenchSteering_TxFailed(&g_motion.bench);
    PatrolRuntime_ApplyCut(mock_now);
    assert(g_motion.bench.fault == 2 && g_motion.center_cal.failure.bench_fault == 2);
    assert(g_motion.center_cal.phase == CENTER_CAL_FAILED && !mock_power);
    CenterCalibrationFailure first = g_motion.center_cal.failure;
    assert(CenterCalibration_KeepAlive(g_motion.center_cal.session_id));
    assert(!CenterCalibration_Jog(g_motion.center_cal.session_id, 1000));
    CenterCalibration_Tick(mock_now + 100);
    assert(memcmp(&first, &g_motion.center_cal.failure, sizeof(first)) == 0);
    assert(CenterCalibration_Format(json, sizeof(json)));

    reset_fixture(); start_ready();
    g_motion.bench.phase = BENCH_FAULT; g_motion.bench.fault = 6; g_motion.bench.step = 23;
    CenterCalibration_Tick(mock_now); /* READY notices faults from any producer */
    assert(g_motion.center_cal.phase == CENTER_CAL_FAILED && g_motion.center_cal.failure.bench_fault == 6);
    assert(g_motion.bench.fault == 6 && !mock_power);

    reset_fixture(); start_ready();
    mock_power = 0;
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_FAILED && (g_motion.center_cal.failure.gate_mask & CENTER_GATE_POWER_OFF));

    reset_fixture(); start_ready();
    g_motion.coordinate_reset_requested = 1;
    PatrolRuntime_ApplyReset(mock_now);
    assert(!mock_power && g_motion.center_cal.failure.reason == CENTER_CAL_REASON_POSITION_EPOCH_RESET);
    reset_fixture(); mock_power = 0; g_motion.coordinate.valid = 1; g_motion.coordinate_reset_requested = 1;
    PatrolRuntime_ApplyReset(mock_now);
    assert(!g_motion.coordinate.valid && g_motion.bench.phase == BENCH_IDLE);

    reset_fixture();
    /* Ordinary driving still loses its anchor and cuts on position loss. */
    for (int i = 0; i < 4; ++i) {
        mock_now += 110; mock_snapshot.coherent_feedback_ms = mock_now;
        PatrolRuntime_UpdateFeedback(mock_now);
    }
    assert(g_motion.coordinate.valid);
    mock_snapshot.steering_feedback_fresh = 0;
    PatrolRuntime_UpdateFeedback(mock_now + 300); PatrolRuntime_ApplyCut(mock_now + 310);
    assert(!mock_power && g_motion.bench.phase == BENCH_FAULT);

    reset_fixture(); start_ready();
    assert(!CenterCalibration_Jog(g_motion.center_cal.session_id, 2000));
    g_motion.center_cal.accumulated_mdeg = 55000;
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, 5000));
    assert(g_motion.center_cal.target_mdeg == 60000);
    g_motion.center_cal.phase = CENTER_CAL_READY;
    assert(!CenterCalibration_Jog(g_motion.center_cal.session_id, 200));
    assert(g_motion.center_cal.target_mdeg == 60000);
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, -5000));
    assert(g_motion.center_cal.target_mdeg == 55000);
    reset_fixture(); start_ready();
    g_motion.center_cal.accumulated_mdeg = -55000;
    assert(CenterCalibration_Jog(g_motion.center_cal.session_id, -5000));
    g_motion.center_cal.phase = CENTER_CAL_READY;
    assert(!CenterCalibration_Jog(g_motion.center_cal.session_id, -200));
    /* A populated failure must remain serializable, not just the zero case. */
    mock_now = UINT32_MAX - 1;
    g_motion.center_cal.phase = CENTER_CAL_READY;
    g_motion.center_cal.failure.reason = CENTER_CAL_REASON_NONE;
    mock_snapshot.coherent_feedback_ms = mock_snapshot.encoder_sample_ms = 0;
    mock_snapshot.encoder_timeout_count = UINT32_MAX;
    g_motion.bench.phase = BENCH_FAULT; g_motion.bench.fault = 0x060A0000;
    CenterCalibration_Tick(mock_now);
    assert(CenterCalibration_Format(json, sizeof(json)));
    /* Exercise the complete runtime tick: a safety revoke between execution
     * selection and CAN submission must discard the motion frame. */
    reset_fixture();
    mock_now = 2000;
    mock_can_ready = mock_revoke_before_send = 1;
    mock_control.session_active = mock_control.enable_request = 1;
    mock_control.lease_deadline_ms = mock_now + 1000;
    mock_control.feedback.power_ready = mock_control.feedback.calibrated = 1;
    mock_control.feedback.steering_online = mock_control.feedback.drive_online = 1;
    mock_control.feedback.feedback_ms = mock_now;
    g_motion.coordinate.valid = 1;
    g_motion.coordinate_calibration_center = mock_config.steering_center_count;
    g_motion.coordinate_absolute_center = mock_config.absolute_center_count;
    g_motion.bench.phase = BENCH_ACTIVE;
    g_motion.bench.step = 17; /* target selected before the safety revoke */
    g_motion.bench.minimum = -100000;
    g_motion.bench.maximum = 100000;
    g_motion.bench_turn = 1;
    PatrolRuntime_Tick();
    assert(!mock_sent && !mock_control.session_active && g_motion.bench.phase == BENCH_STOPPING);
    assert(!g_motion.bench.pending);
    /* Full commit: clear, write, and three fresh samples straddling zero. */
    reset_fixture(); start_ready();
    assert(CenterCalibration_Commit(g_motion.center_cal.session_id));
    for (int i=0;i<3;i++) {
        mock_now+=20; mock_snapshot.coherent_feedback_ms=mock_now;
        CenterCalibration_Tick(mock_now);
    }
    assert(g_motion.center_cal.phase==CENTER_CAL_ENCODER_WRITE);
    g_motion.center_cal.encoder_write_state=2;
    CenterCalibration_Tick(++mock_now);
    assert(g_motion.center_cal.phase==CENTER_CAL_ENCODER_VERIFY);
    const uint32_t samples[3]={4095,0,1};
    for (int i=0;i<3;i++) {
        mock_now+=20; mock_snapshot.encoder_sample_ms=mock_now;
        mock_snapshot.encoder_count=samples[i];
        CenterCalibration_Tick(mock_now);
        if(i<2) assert(g_motion.center_cal.phase==CENTER_CAL_ENCODER_VERIFY);
    }
    assert(g_motion.center_cal.phase==CENTER_CAL_COMPLETE && !mock_power);
    assert(CenterCalibration_Format(json, sizeof(json)) && strstr(json, "\"state\":\"DONE\""));
    mock_now = g_motion.center_cal.deadline_ms;
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase == CENTER_CAL_IDLE && mock_config.calibrated);
    reset_fixture(); start_ready();
    g_motion.center_cal.phase=CENTER_CAL_ENCODER_VERIFY;
    mock_snapshot.encoder_count=4096;
    CenterCalibration_Tick(mock_now);
    assert(g_motion.center_cal.phase==CENTER_CAL_FAILED);
    /* First cause survives secondary shutdown and records unknown sample ages. */
    MotionFaultRecord record;
    reset_fixture();
    g_motion.diagnostics.drive_status_word=0xBF;
    g_motion.diagnostics.drive_code_ms[0]=900;
    g_motion.diagnostics.drive_code_ms[1]=0;
    /* All previous cases retain their original first record until reboot. */
    assert(PatrolRuntime_GetFault(&record));
    uint32_t first_at=record.at_ms, source=record.source;
    mock_now+=100; PatrolRuntime_CaptureFault(1,99,123);
    assert(PatrolRuntime_GetFault(&record) && record.at_ms==first_at && record.source==source);
    puts("PASS production center READY fault chain, preserved evidence, epoch ownership and +/-5 degree limits");
    return 0;
}
