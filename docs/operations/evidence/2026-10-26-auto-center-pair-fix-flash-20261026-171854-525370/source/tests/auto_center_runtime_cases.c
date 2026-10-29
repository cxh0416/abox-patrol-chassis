#define CENTER_HOST_REAL_CONTROL
#include "center_runtime_stubs.h"

static void feedback(int32_t angle_mdeg)
{
    mock_snapshot.steering_position_count = (int32_t)((int64_t)angle_mdeg * 2710800 / 1000000);
    mock_snapshot.encoder_count = 2048U + (int32_t)((int64_t)angle_mdeg * 55250 / 1000000);
    mock_snapshot.coherent_feedback_ms = mock_snapshot.encoder_sample_ms = mock_now;
    mock_snapshot.steering_position_ms = mock_now;
    g_motion.diagnostics.steering_position_count = mock_snapshot.steering_position_count;
    g_motion.diagnostics.encoder_count = mock_snapshot.encoder_count;
    g_motion.diagnostics.left_rpm_ms = g_motion.diagnostics.right_rpm_ms = mock_now;
    PatrolRuntime_UpdateFeedback(mock_now);
}

static void reset_fixture(int32_t angle_mdeg)
{
    memset(&g_motion, 0, sizeof(g_motion));
    memset(&mock_config, 0, sizeof(mock_config));
    memset(&mock_snapshot, 0, sizeof(mock_snapshot));
    memset(&mock_control, 0, sizeof(mock_control));
    mock_power = mock_profile_ready = 1U;
    mock_can_ready = mock_revoke_before_send = mock_inhibit_before_send = mock_sent = 0U;
    mock_bus_off = mock_ota = 0U;
    mock_now = 1000U;
    mock_config.calibrated = 1U;
    mock_config.absolute_center_count = 2048U;
    mock_config.absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.absolute_direction = 1;
    mock_config.steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
    mock_config.steering_direction = 1;
    mock_snapshot.steering_online = mock_snapshot.drive_online = 1U;
    mock_snapshot.steering_feedback_fresh = mock_snapshot.drive_feedback_fresh = 1U;
    mock_snapshot.steering_status_fresh = mock_snapshot.drive_status_fresh = 1U;
    mock_snapshot.encoder_online = mock_snapshot.encoder_config_valid = 1U;
    mock_snapshot.steering_status_word = mock_snapshot.drive_status_word = 0x21U;
    g_motion.coordinate.valid = 1U;
    g_motion.coordinate_calibration_center = mock_config.steering_center_count;
    g_motion.coordinate_absolute_center = mock_config.absolute_center_count;
    PatrolRuntime_Init();
    feedback(angle_mdeg);
}

static void active_steering(void)
{
    g_motion.bench.phase = BENCH_ACTIVE;
    g_motion.bench.step = 16U;
    g_motion.bench.limit_mdeg = 30000U;
    g_motion.bench.minimum = -82680;
    g_motion.bench.maximum = 82680;
    g_motion.bench.applied = mock_snapshot.steering_position_count;
    g_motion.bench.applied_profile_rpm = 300U;
}

int main(void)
{
    /* Real safety arbitration and runtime: idle power at 21 degrees owns a
     * steering-only return without inventing a remote session or drive permit. */
    reset_fixture(21000);
    mock_can_ready = 1U;
    PatrolRuntime_Tick();
    assert(PatrolRuntime_AutoCenterActive() && g_motion.auto_center.moving);
    assert(g_motion.bench.phase == BENCH_PREPARING && g_motion.bench.desired == 0);
    assert(mock_sent == 1U && mock_last_frame.id == 0 && mock_last_frame.data[1] == 1U);
    assert(!mock_control.session_active && !mock_control.enable_request && g_motion.drive.phase == BENCH_IDLE);
    reset_fixture(-21000); PatrolRuntime_Tick();
    assert(g_motion.auto_center.moving && g_motion.bench.desired == 0);

    /* Pair skew pauses an already started local return through the normal
     * bounded stop path. It must not become an emergency cut/power fault. */
    reset_fixture(21000); PatrolRuntime_Tick();
    assert(g_motion.auto_center.moving && g_motion.bench.phase == BENCH_PREPARING);
    mock_control.feedback.angle_pair_valid = 0U;
    PatrolRuntime_Tick();
    assert(g_motion.auto_center.requested && g_motion.auto_center.moving);
    assert(g_motion.bench.phase == BENCH_PREPARING);
    assert(!mock_control.fault_latched && mock_power);

    /* A valid boundary-buffer sample is never replayed as a >30 degree hold. */
    reset_fixture(30100); active_steering();
    g_motion.drive.phase = BENCH_ACTIVE; g_motion.drive.step = 12U;
    PatrolRuntime_Tick();
    assert(g_motion.auto_center.requested && !g_motion.auto_center.moving);
    assert(g_motion.auto_center.hold_mdeg == 30000 && g_motion.bench.phase != BENCH_FAULT);

    /* Expiry clears traction and remote ownership, parks the drive first,
     * and keeps the steering at its measured angle while zero is confirmed. */
    reset_fixture(21000); active_steering();
    assert(ChassisControl_Apply(&mock_control, "acquire", "remote", 1, 1000, 100, 21000, mock_now));
    g_motion.drive.phase = BENCH_ACTIVE; g_motion.drive.step = 12U;
    mock_now += 1001U; feedback(21000);
    ChassisControl_Tick(&mock_control, mock_now);
    assert(mock_control.stop_reason == CHASSIS_STOP_LEASE_EXPIRED && !mock_control.session_active);
    PatrolRuntime_Tick();
    assert(g_motion.drive.phase == BENCH_STOPPING && g_motion.drive.desired == 0);
    assert(g_motion.auto_center.requested && !g_motion.auto_center.moving);
    assert(abs(g_motion.bench.desired - mock_snapshot.steering_position_count) <= 3);
    /* BenchDrive's production stop path separately verifies three pairs of
     * zero samples and disabled status (covered by bench_drive_test). */
    g_motion.drive.phase = BENCH_IDLE;
    g_motion.bench.step = 16U; ++mock_now; feedback(21000);
    PatrolRuntime_Tick();
    assert(g_motion.auto_center.moving && g_motion.bench.desired == 0);
    assert(mock_control.target_rpm == 0 && !mock_control.enable_request);

    /* Three independent fresh dual-angle samples finish the return. */
    for (int i = 0; i < 3; ++i) {
        mock_now += 20U; feedback(500); PatrolRuntime_Tick();
        if (i < 2) assert(g_motion.auto_center.requested);
    }
    assert(!g_motion.auto_center.requested && g_motion.bench.phase == BENCH_STOPPING);

    /* A new validated target preempts local centering; cleanup must not revoke
     * the new session just because the previous steering stop is finishing. */
    reset_fixture(21000); active_steering(); PatrolRuntime_Tick();
    assert(ChassisControl_Apply(&mock_control, "acquire", "takeover", 1, 1000, 0, -10000, mock_now));
    g_motion.bench.phase = BENCH_STOPPING; g_motion.bench.step = 21U; g_motion.bench.stop_ms = mock_now;
    PatrolRuntime_Tick();
    assert(!g_motion.auto_center.requested && mock_control.session_active && mock_control.enable_request);

    /* SAFETY_STOP parks, including a replay/foreign-session stop. An unrelated
     * temporary feedback error must not silently remove this inhibit. */
    reset_fixture(21000); active_steering(); PatrolRuntime_Tick();
    assert(ChassisControl_Apply(&mock_control, "stop", "foreign", 1, 1000, 0, 0, mock_now));
    PatrolRuntime_Tick();
    assert(!g_motion.auto_center.requested && g_motion.bench.phase == BENCH_STOPPING);
    mock_control.feedback.drive_online = 0U; ChassisControl_Tick(&mock_control, mock_now);
    feedback(21000); g_motion.bench.phase = BENCH_IDLE;
    PatrolRuntime_Tick();
    assert(!g_motion.auto_center.requested && mock_control.auto_center_inhibited);

    /* Recheck an explicit stop that arrives after CAN frame selection. */
    reset_fixture(21000); active_steering();
    g_motion.bench_turn = 1U;
    mock_can_ready = mock_inhibit_before_send = 1U;
    PatrolRuntime_Tick();
    assert(!mock_sent && g_motion.bench.phase == BENCH_STOPPING);

    /* Each gate blocks an idle return without an actuator command. */
    for (int gate = 0; gate < 14; ++gate) {
        reset_fixture(21000);
        switch (gate) {
        case 0: mock_bus_off = 1U; break;
        case 1: mock_ota = 1U; break;
        case 2: mock_snapshot.drive_fault = 1U; break;
        case 3: mock_snapshot.steering_feedback_fresh = 0U; break;
        case 4: mock_config.calibrated = 0U; break;
        case 5: mock_snapshot.drive_online = 0U; break;
        case 6: mock_snapshot.encoder_online = 0U; break;
        case 7: mock_power = 0U; break;
        case 8: g_motion.center_cal.phase = CENTER_CAL_READY; break;
        case 12: mock_snapshot.left_actual_rpm = 21; break;
        }
        feedback(21000);
        if (gate == 9) mock_control.feedback.encoder_actual_mdeg += 3000;
        if (gate == 10) mock_control.feedback.encoder_range_valid = 0U;
        if (gate == 11) { mock_control.fault_latched = 1U; mock_control.stop_reason = CHASSIS_STOP_EXECUTION_FAULT; }
        if (gate == 13) mock_control.feedback.angle_pair_valid = 0U;
        PatrolRuntime_Tick();
        assert(!g_motion.auto_center.requested && !mock_sent);
        assert(g_motion.bench.phase == BENCH_IDLE || g_motion.bench.phase == BENCH_FAULT);
    }

    /* Feedback loss during a return uses the existing fault/cut path. */
    reset_fixture(21000); active_steering(); PatrolRuntime_Tick();
    mock_now += 251U; ChassisControl_Tick(&mock_control, mock_now); PatrolRuntime_Tick();
    assert(!mock_power && mock_control.fault_latched && !g_motion.auto_center.requested);

    /* Bounded return: never restart an indefinitely stalled steering axis. */
    reset_fixture(21000); active_steering(); PatrolRuntime_Tick();
    mock_now += PATROL_AUTO_CENTER_TIMEOUT_MS; feedback(21000); PatrolRuntime_Tick();
    assert(mock_control.fault_latched && !mock_power && g_motion.auto_center.failed);

    /* Sample reuse, second-sensor completion and tick wrap use production FSM. */
    PatrolAutoCenter center = {0}; ChassisFeedback f = {0};
    PatrolAutoCenter_Request(&center, UINT32_MAX - 100U, 21000);
    f.feedback_ms = 1U; f.encoder_actual_mdeg = 2000;
    assert(PatrolAutoCenter_Tick(&center, 1U, 1U, 1U, &f));
    assert(!center.centered_samples);
    f.encoder_actual_mdeg = 0; f.feedback_ms = 2U;
    assert(PatrolAutoCenter_Tick(&center, 2U, 1U, 1U, &f));
    assert(PatrolAutoCenter_Tick(&center, 3U, 1U, 1U, &f) && center.centered_samples == 1U);
    f.feedback_ms = 4U; assert(PatrolAutoCenter_Tick(&center, 4U, 1U, 1U, &f));
    f.feedback_ms = 5U; assert(!PatrolAutoCenter_Tick(&center, 5U, 1U, 1U, &f) && !center.requested);
    puts("PASS local centering, lease expiry, drive park, takeover, safety stop, gates and bounded completion");
    return 0;
}
