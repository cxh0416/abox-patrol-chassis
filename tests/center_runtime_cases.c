
static void reset_fixture(void)
{
    memset(&g_center_cal, 0, sizeof(g_center_cal));
    memset(&mock_control, 0, sizeof(mock_control));
    memset(&mock_snapshot, 0, sizeof(mock_snapshot));
    memset(&mock_config, 0, sizeof(mock_config));
    BenchSteering_Init(&g_bench); BenchDrive_Init(&g_drive);
    SteeringCoordinate_Reset(&g_coordinate);
    g_coordinate_reset_requested = mock_bus_off = mock_ota = mock_permit = 0;
    g_feedback_gate_mask = g_bench_fault_gate_mask = 0;
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
    assert(VcuRtos_CenterCalibrationStart());
    center_update_relative(&mock_snapshot);
    for (int i = 0; i < 3; ++i) {
        mock_now += 20;
        mock_snapshot.coherent_feedback_ms = mock_now;
        center_calibration_tick(mock_now);
    }
    assert(g_center_cal.phase == CENTER_CAL_READY && mock_power);
}

int main(void)
{
    char json[900];
    reset_fixture();
    g_coordinate.valid = 1;
    start_ready();
    assert(!g_coordinate.valid); /* discard ordinary anchor on session start */
    for (int i = 0; i < 6; ++i) {
        mock_now += 100;
        mock_snapshot.coherent_feedback_ms = mock_now;
        update_coordinate(mock_now);
    }
    assert(!g_coordinate.valid && mock_power); /* never re-acquire during READY */
    g_bench.step = 23; /* the actual executor's stop-complete state */
    mock_snapshot.steering_feedback_fresh = 0;
    mock_snapshot.drive_feedback_fresh = mock_snapshot.drive_status_fresh = 0;
    update_coordinate(mock_now + 300);
    center_calibration_tick(mock_now);
    apply_cut(mock_now);
    assert(g_center_cal.phase == CENTER_CAL_READY && g_bench.phase == BENCH_IDLE && mock_power);
    assert(!VcuRtos_CenterCalibrationJog(g_center_cal.session_id, 5000));
    assert(g_center_cal.accumulated_mdeg == 0);
    assert(g_center_cal.last_reject_reason == CENTER_CAL_REASON_GATE_NOT_READY);
    assert(VcuRtos_CenterCalibrationKeepAlive(g_center_cal.session_id));
    assert(g_center_cal.last_reject_reason == CENTER_CAL_REASON_GATE_NOT_READY);
    assert(VcuRtos_FormatCenterCalibration(json, sizeof(json)));
    assert(strstr(json, "\"reason\":\"gate_not_ready\""));
    mock_snapshot.steering_feedback_fresh = mock_snapshot.drive_feedback_fresh = mock_snapshot.drive_status_fresh = 1;
    assert(VcuRtos_CenterCalibrationJog(g_center_cal.session_id, 5000));
    assert(g_center_cal.target_mdeg == 5000 && g_center_cal.phase == CENTER_CAL_JOGGING);
    mock_snapshot.steering_feedback_fresh = 0;
    center_calibration_tick(mock_now);
    assert(g_center_cal.phase == CENTER_CAL_FAILED && !mock_power && mock_control.fault_latched);
    assert(g_center_cal.failure.reason == CENTER_CAL_REASON_GATE_NOT_READY);

    reset_fixture(); start_ready();
    g_bench.phase = BENCH_ACTIVE; g_bench.step = 17;
    BenchSteering_TxFailed(&g_bench);
    apply_cut(mock_now);
    assert(g_bench.fault == 2 && g_center_cal.failure.bench_fault == 2);
    assert(g_center_cal.phase == CENTER_CAL_FAILED && !mock_power);
    CenterCalibrationFailure first = g_center_cal.failure;
    assert(VcuRtos_CenterCalibrationKeepAlive(g_center_cal.session_id));
    assert(!VcuRtos_CenterCalibrationJog(g_center_cal.session_id, 1000));
    center_calibration_tick(mock_now + 100);
    assert(memcmp(&first, &g_center_cal.failure, sizeof(first)) == 0);
    assert(VcuRtos_FormatCenterCalibration(json, sizeof(json)));

    reset_fixture(); start_ready();
    g_bench.phase = BENCH_FAULT; g_bench.fault = 6; g_bench.step = 23;
    center_calibration_tick(mock_now); /* READY notices faults from any producer */
    assert(g_center_cal.phase == CENTER_CAL_FAILED && g_center_cal.failure.bench_fault == 6);
    assert(g_bench.fault == 6 && !mock_power);

    reset_fixture(); start_ready();
    mock_power = 0;
    center_calibration_tick(mock_now);
    assert(g_center_cal.phase == CENTER_CAL_FAILED && (g_center_cal.failure.gate_mask & CENTER_GATE_POWER_OFF));

    reset_fixture(); start_ready();
    g_coordinate_reset_requested = 1;
    apply_reset(mock_now);
    assert(!mock_power && g_center_cal.failure.reason == CENTER_CAL_REASON_POSITION_EPOCH_RESET);
    reset_fixture(); mock_power = 0; g_coordinate.valid = 1; g_coordinate_reset_requested = 1;
    apply_reset(mock_now);
    assert(!g_coordinate.valid && g_bench.phase == BENCH_IDLE);

    reset_fixture();
    /* Ordinary driving still loses its anchor and cuts on position loss. */
    for (int i = 0; i < 4; ++i) {
        mock_now += 110; mock_snapshot.coherent_feedback_ms = mock_now;
        update_coordinate(mock_now);
    }
    assert(g_coordinate.valid);
    mock_snapshot.steering_feedback_fresh = 0;
    update_coordinate(mock_now + 300); apply_cut(mock_now + 310);
    assert(!mock_power && g_bench.phase == BENCH_FAULT);

    reset_fixture(); start_ready();
    assert(!VcuRtos_CenterCalibrationJog(g_center_cal.session_id, 2000));
    g_center_cal.accumulated_mdeg = 55000;
    assert(VcuRtos_CenterCalibrationJog(g_center_cal.session_id, 5000));
    assert(g_center_cal.target_mdeg == 60000);
    g_center_cal.phase = CENTER_CAL_READY;
    assert(!VcuRtos_CenterCalibrationJog(g_center_cal.session_id, 200));
    assert(g_center_cal.target_mdeg == 60000);
    assert(VcuRtos_CenterCalibrationJog(g_center_cal.session_id, -5000));
    assert(g_center_cal.target_mdeg == 55000);
    reset_fixture(); start_ready();
    g_center_cal.accumulated_mdeg = -55000;
    assert(VcuRtos_CenterCalibrationJog(g_center_cal.session_id, -5000));
    g_center_cal.phase = CENTER_CAL_READY;
    assert(!VcuRtos_CenterCalibrationJog(g_center_cal.session_id, -200));
    /* A populated failure must remain serializable, not just the zero case. */
    mock_now = UINT32_MAX - 1;
    g_center_cal.phase = CENTER_CAL_READY;
    g_center_cal.failure.reason = CENTER_CAL_REASON_NONE;
    mock_snapshot.coherent_feedback_ms = mock_snapshot.encoder_sample_ms = 0;
    mock_snapshot.encoder_timeout_count = UINT32_MAX;
    g_bench.phase = BENCH_FAULT; g_bench.fault = 0x060A0000;
    center_calibration_tick(mock_now);
    assert(VcuRtos_FormatCenterCalibration(json, sizeof(json)));
    puts("PASS production center READY fault chain, preserved evidence, epoch ownership and +/-5 degree limits");
    return 0;
}
