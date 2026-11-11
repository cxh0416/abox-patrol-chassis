#define CENTER_HOST_REAL_CONTROL
#include "center_runtime_stubs.h"

_Static_assert(sizeof(ChassisFeedback) == 28U, "feedback must reuse padding");
_Static_assert(sizeof(SteeringCoordinate) == 24U, "coordinate must reuse padding");

static void fixture(void)
{
    memset(&g_motion, 0, sizeof(g_motion));
    memset(&mock_config, 0, sizeof(mock_config));
    memset(&mock_snapshot, 0, sizeof(mock_snapshot));
    ChassisControl_Init(&mock_control);
    mock_now = 1000U;
    mock_power = mock_profile_ready = 1U;
    mock_bus_off = mock_ota = mock_cal_prepare = mock_can_ready = mock_sent = 0U;
    mock_config.calibrated = 1U;
    mock_config.steering_center_count = 32632;
    mock_config.steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
    mock_config.absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.steering_direction = mock_config.absolute_direction = 1;
    mock_snapshot.steering_online = mock_snapshot.drive_online = 1U;
    mock_snapshot.steering_feedback_fresh = mock_snapshot.drive_feedback_fresh = 1U;
    mock_snapshot.steering_status_fresh = mock_snapshot.drive_status_fresh = 1U;
    mock_snapshot.encoder_online = mock_snapshot.encoder_config_valid = 1U;
    mock_snapshot.steering_status_word = mock_snapshot.drive_status_word = 0x21U;
    mock_snapshot.steering_position_count = -83815;
    mock_snapshot.encoder_count = 3U;
    PatrolRuntime_Init();
}

static void sample(uint32_t delta)
{
    mock_now += delta;
    mock_snapshot.coherent_feedback_ms = mock_snapshot.encoder_sample_ms = mock_now;
    PatrolRuntime_UpdateFeedback(mock_now);
    ChassisControl_Tick(&mock_control, mock_now);
}

static void ready(void)
{
    sample(0); sample(150); sample(150);
    assert(g_motion.coordinate.valid && ChassisControl_SafetyReady(&mock_control, mock_now));
}

int main(void)
{
    /* Exact field failure: the old center implies -42.956 deg before startup. */
    fixture(); sample(0);
    assert(mock_control.feedback.steering_actual_mdeg == -42956);
    assert(mock_control.feedback.steering_angle_unavailable && !mock_control.fault_latched);
    assert(mock_control.stop_reason == CHASSIS_STOP_POWER_NOT_READY);
    assert(!ChassisControl_Apply(&mock_control,"acquire","test",1,1000,100,0,mock_now));
    sample(150); sample(150);
    assert(g_motion.coordinate.valid && !mock_control.feedback.steering_angle_unavailable);
    assert(g_motion.coordinate.runtime_center == -83962);
    assert(mock_control.feedback.steering_actual_mdeg == 54);
    assert(!mock_control.fault_latched && !mock_control.enable_request);
    puts("PASS old saved center cannot latch motor range before runtime coordinate is valid");

    /* Bootstrap preserves physical offset; it does not call the present point zero. */
    fixture(); mock_snapshot.encoder_count = 553U; ready();
    assert(mock_control.feedback.steering_actual_mdeg >= 9990);
    assert(mock_control.feedback.steering_actual_mdeg <= 10020);
    assert(mock_config.steering_center_count == 32632);
    puts("PASS stationary bootstrap preserves a ten-degree independent angle and Flash calibration");

    /* Independent range checks are never bypassed by unavailable motor coordinates. */
    fixture(); mock_snapshot.encoder_count = 1700U; sample(0);
    assert(mock_control.fault_latched && mock_control.stop_reason == CHASSIS_STOP_ENCODER_RANGE);
    fixture(); mock_snapshot.encoder_count = 4096U; sample(0);
    assert(mock_control.fault_latched && !g_motion.coordinate.valid);
    fixture(); ready();
    mock_snapshot.steering_position_count += 84040; sample(20);
    assert(mock_control.fault_latched && mock_control.stop_reason == CHASSIS_STOP_ENCODER_RANGE);
    puts("PASS independent raw/range and valid motor-angle hard limits still latch");

    /* PREPARE/START owns a relative maintenance coordinate, never an ordinary permit. */
    fixture(); mock_cal_prepare = 1U; sample(0);
    assert(mock_control.feedback.motion_inhibited && !mock_control.fault_latched);
    assert(!g_motion.coordinate.valid && !mock_control.feedback.power_ready);
    assert(CenterCalibration_Start()); mock_cal_prepare = 0U;
    for (unsigned i=0;i<3;i++) { sample(20); CenterCalibration_Tick(mock_now); }
    assert(g_motion.center_cal.phase == CENTER_CAL_READY && !mock_control.fault_latched);
    assert(!ChassisControl_Apply(&mock_control,"acquire","test",2,1000,0,0,mock_now));
    assert(CenterCalibration_Commit(g_motion.center_cal.session_id));
    for (unsigned i=0;i<3;i++) { sample(20); CenterCalibration_Tick(mock_now); }
    assert(g_motion.center_cal.phase == CENTER_CAL_ENCODER_WRITE);
    g_motion.center_cal.encoder_write_state = 2U;
    CenterCalibration_Tick(++mock_now);
    const uint32_t zeroes[3] = {4095U,0U,1U};
    for (unsigned i=0;i<3;i++) {
        mock_snapshot.encoder_count=zeroes[i]; sample(20); CenterCalibration_Tick(mock_now);
    }
    assert(g_motion.center_cal.phase == CENTER_CAL_COMPLETE && !mock_power);
    assert(!mock_control.enable_request && !mock_control.session_active);
    puts("PASS exact old-center field case enters maintenance, commits three zero reads and powers off");

    /* A retired live coordinate cannot be re-acquired while powered. */
    fixture(); ready();
    mock_snapshot.encoder_online = 0U; sample(20);
    assert(!g_motion.coordinate.valid && g_motion.coordinate.blocked);
    mock_snapshot.encoder_online = 1U;
    for (unsigned i=0;i<8;i++) sample(100);
    assert(!g_motion.coordinate.valid && !mock_control.enable_request);
    mock_power=0U; sample(20); assert(!g_motion.coordinate.blocked);
    mock_power=1U; ready();
    puts("PASS live epoch loss stays blocked until explicit power-off and a new bootstrap");

    fixture(); sample(0);
    mock_snapshot.left_actual_rpm = 30; /* Never stable/stopped. */
    sample(6999); assert(mock_power);
    sample(1); assert(!mock_power && !mock_control.fault_latched && !g_motion.coordinate.valid);
    fixture(); mock_now=UINT32_MAX-1000U; sample(0);
    mock_snapshot.left_actual_rpm=30; sample(7000);
    assert(!mock_power && !mock_control.fault_latched);
    puts("PASS startup wait is bounded at seven seconds, including monotonic wrap");

    fixture(); ready();
    assert(ChassisControl_Apply(&mock_control,"acquire","test",1,1000,400,0,mock_now));
    g_motion.bench.phase=BENCH_ACTIVE; g_motion.bench.step=16;
    g_motion.bench.minimum=INT32_MIN; g_motion.bench.maximum=INT32_MAX;
    g_motion.bench.limit_mdeg=30000; g_motion.bench.center=g_motion.coordinate.runtime_center;
    g_motion.drive.phase=BENCH_ACTIVE; g_motion.drive.step=12;
    g_motion.drive.applied=g_motion.drive.applied_left=g_motion.drive.applied_right=400;
    ChassisControl_CollisionInputs(&mock_control,1,1,mock_now);
    PatrolRuntime_Tick();
    assert(g_motion.drive.phase==BENCH_ACTIVE && g_motion.drive.reversing==3U);
    assert(g_motion.drive.writing==0 && !mock_control.fault_latched && !mock_control.fault_pending);
    assert(mock_control.session_active && mock_control.enable_request && mock_power);
    assert(g_motion.bench.phase==BENCH_ACTIVE && !g_motion.auto_center.requested);
    puts("PASS real runtime collision uses enabled zero hold, preserves authorization and cancels auto centering");
    return 0;
}
