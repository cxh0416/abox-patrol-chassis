#define CENTER_HOST_REAL_CONTROL
#define DRIVE_BRAKE_RUNTIME_TEST
#include "center_runtime_stubs.h"

static void fixture(void)
{
    memset(&g_motion,0,sizeof(g_motion));
    memset(&mock_config,0,sizeof(mock_config));
    memset(&mock_snapshot,0,sizeof(mock_snapshot));
    ChassisControl_Init(&mock_control);
    mock_now=1000;
    mock_power=mock_profile_ready=mock_can_ready=1;
    mock_bus_off=mock_ota=mock_revoke_before_send=mock_inhibit_before_send=mock_sent=0;
    mock_brake_after_take=0;
    mock_config.calibrated=1;
    mock_config.absolute_counts_per_degree_milli=VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.absolute_direction=mock_config.steering_direction=1;
    mock_config.steering_counts_per_degree_milli=VEHICLE_STEERING_COUNTS_MILLI;
    mock_snapshot.steering_online=mock_snapshot.drive_online=1;
    mock_snapshot.steering_feedback_fresh=mock_snapshot.drive_feedback_fresh=1;
    mock_snapshot.steering_status_fresh=mock_snapshot.drive_status_fresh=1;
    mock_snapshot.encoder_online=mock_snapshot.encoder_config_valid=1;
    mock_snapshot.steering_status_word=0x27;
    mock_snapshot.drive_status_word=7;
    mock_snapshot.steering_position_count=13554; /* 5 wheel degrees */
    mock_snapshot.encoder_count=276;
    mock_snapshot.coherent_feedback_ms=mock_now;
    g_motion.coordinate.valid=1;
    PatrolRuntime_Init();
    g_motion.drive.phase=BENCH_ACTIVE; g_motion.drive.step=12;
    g_motion.drive.applied=1000; g_motion.drive.direction=1;
    g_motion.bench.phase=BENCH_ACTIVE; g_motion.bench.step=16;
    g_motion.bench.center=0; g_motion.bench.limit_mdeg=30000;
    g_motion.bench.minimum=-82680; g_motion.bench.maximum=82680;
    g_motion.bench.applied=13554; g_motion.bench.applied_profile_rpm=VEHICLE_STEERING_PROFILE_CREEP_RPM;
    PatrolRuntime_UpdateFeedback(mock_now);
    assert(ChassisControl_Apply(&mock_control,"acquire","remote",1,1000,1000,5000,mock_now));
}
static void next_sample(void)
{
    mock_now+=20;
    g_motion.diagnostics.left_rpm_ms=g_motion.diagnostics.right_rpm_ms=mock_now;
    mock_snapshot.coherent_feedback_ms=mock_now;
    PatrolRuntime_UpdateFeedback(mock_now);
}
static void acknowledge(const PatrolCanFrame* frame)
{
    PatrolCanFrame reply=*frame;
    reply.id=0x582;
    if (frame->data[0]==0x40) {
        reply=PatrolCanopen_SdoWrite16(2,0x6041,0,0x21);
        reply.id=0x582; reply.data[0]=0x4b;
        mock_snapshot.drive_status_word=0x21;
    } else reply.data[0]=0x60;
    PatrolRuntime_OnCanFrame(&reply,mock_now);
}
int main(void)
{
    fixture(); ChassisControl_SetDriveBrake(&mock_control,1);
    for(unsigned i=0;i<15 && g_motion.drive.phase!=BENCH_IDLE;i++) {
        uint8_t sent=mock_sent;
        next_sample(); PatrolRuntime_Tick();
        assert(mock_control.enable_request && mock_control.session_active && !mock_control.fault_latched);
        assert(mock_power && g_motion.bench.phase==BENCH_ACTIVE);
        if (mock_sent!=sent) {
            assert(mock_last_frame.id==0x602 || mock_last_frame.id==0);
            if (mock_last_frame.id==0x602) {
                uint16_t index=(uint16_t)mock_last_frame.data[1] | (uint16_t)mock_last_frame.data[2]<<8;
                if ((index==0x60ff || index==0x68ff) && mock_last_frame.data[0]!=0x40)
                    for(unsigned j=4;j<8;j++) assert(mock_last_frame.data[j]==0);
                acknowledge(&mock_last_frame);
            }
        }
    }
    assert(g_motion.drive.phase==BENCH_IDLE && !g_motion.drive.cut_power);
    next_sample(); PatrolRuntime_Tick(); assert(g_motion.drive.phase==BENCH_IDLE);
    /* Steering can receive a new target while the drive stays disabled. */
    assert(ChassisControl_Apply(&mock_control,"renew","remote",2,1000,1000,-5000,mock_now));
    PatrolRuntime_Tick();
    assert(g_motion.drive.phase==BENCH_IDLE && g_motion.bench.phase==BENCH_ACTIVE);
    assert(g_motion.bench.desired==-13554 && mock_control.target_rpm==0 && mock_power);
    /* Release uses the ordinary drive initialization; it never jumps to ACTIVE. */
    ChassisControl_SetDriveBrake(&mock_control,0);
    assert(ChassisControl_Apply(&mock_control,"renew","remote",3,1000,100,-5000,mock_now));
    PatrolRuntime_Tick(); assert(g_motion.drive.phase==BENCH_PREPARING);

    /* B arrives after either nonzero wheel command was already selected. */
    for(unsigned step=13;step<=14;step++) {
        fixture(); g_motion.drive.step=(uint8_t)step; g_motion.drive.writing=100;
        g_motion.drive_turn=g_motion.bench_turn=1;
        mock_brake_after_take=1;
        PatrolRuntime_Tick();
        assert(mock_control.drive_brake && g_motion.drive.phase==BENCH_STOPPING);
        assert(mock_sent==0 && g_motion.bench.phase==BENCH_ACTIVE && mock_control.enable_request);
        next_sample(); PatrolRuntime_Tick();
        assert(mock_last_frame.id==0x602 && mock_last_frame.data[1]==0xff && mock_last_frame.data[2]==0x60);
        for(unsigned j=4;j<8;j++) assert(mock_last_frame.data[j]==0);
    }

    /* B can interrupt drive preparation, and early release cannot skip stop. */
    fixture(); g_motion.drive.phase=BENCH_PREPARING; g_motion.drive.step=9;
    ChassisControl_SetDriveBrake(&mock_control,1); PatrolRuntime_Tick();
    assert(g_motion.drive.phase==BENCH_STOPPING && g_motion.bench.phase==BENCH_ACTIVE);
    ChassisControl_SetDriveBrake(&mock_control,0);
    assert(ChassisControl_Apply(&mock_control,"renew","remote",2,1000,100,5000,mock_now));
    next_sample(); PatrolRuntime_Tick();
    assert(g_motion.drive.phase==BENCH_STOPPING && mock_control.enable_request);

    /* An actual stop failure still cuts power and latches the common fault. */
    fixture(); ChassisControl_SetDriveBrake(&mock_control,1);
    PatrolRuntime_Tick(); mock_can_ready=0; mock_now+=6000;
    mock_snapshot.coherent_feedback_ms=mock_now;
    PatrolRuntime_UpdateFeedback(mock_now); PatrolRuntime_Tick();
    assert(mock_control.fault_latched && !mock_power);
    puts("Traction brake runtime: bounded zero/disable, steering continuity, release, race and timeout: PASS");
    return 0;
}
