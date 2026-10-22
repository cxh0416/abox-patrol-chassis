#define main original_brake_regression_main
#include "../../tests/drive_brake_runtime_cases.c"
#undef main

// 重现平台报告的转向 step30 事务超时，同时观察仍在行走时的紧急退出行为。
int main(void)
{
    fixture();
    g_motion.drive.writing=g_motion.drive.applied=1000;
    g_motion.diagnostics.left_actual_rpm=997;
    g_motion.diagnostics.right_actual_rpm=-1000;
    g_motion.bench.step=30; g_motion.bench.index=0x6081;
    g_motion.bench.read=1; g_motion.bench.width=4;
    g_motion.bench.pending=1; g_motion.bench.sent_ms=mock_now-100;
    g_motion.bench.expected=g_motion.bench.speed_write_rpm=180;
    mock_sent=0;
    PatrolRuntime_Tick();
    MotionFaultRecord captured;
    assert(PatrolRuntime_GetFault(&captured));
    assert(captured.reason==0 && captured.bench_phase==4 && captured.bench_step==30 && captured.bench_fault==6);
    assert(captured.drive_phase==2 && captured.drive_step==12 && captured.drive_fault==0);
    assert(captured.target_rpm==1000 && captured.left_rpm==997 && captured.right_rpm==-1000);
    assert(mock_control.fault_latched && !mock_power && mock_sent==0);
    assert(g_motion.drive.phase==BENCH_FAULT && g_motion.drive.writing==1000);
    for (unsigned i=0;i<25;i++) { mock_now+=10; PatrolRuntime_Tick(); }
    assert(mock_sent==0);
    puts("Platform tuple reproduced: reason0, steer4/30/6, drive2/12/0, target1000, measured997/-1000");
    puts("Protection sets power command OFF and drive FAULT, sends no CAN zeros/shutdown; drive writing remains1000");

    for (unsigned delay=99;delay<=100;delay++) {
        BenchSteering b;BenchSteering_Init(&b);
        b.phase=BENCH_ACTIVE;b.step=30;b.index=0x6081;b.read=1;b.width=4;
        b.pending=1;b.sent_ms=1000;b.expected=b.speed_write_rpm=180;
        PatrolCanFrame response=PatrolCanopen_SdoWrite32(1,0x6081,0,180);
        response.id=0x581;response.data[0]=0x43;
        assert(BenchSteering_OnFrame(&b,&response,1000+delay));
        if(delay==99) assert(b.phase==BENCH_ACTIVE && b.fault==0);
        else assert(b.phase==BENCH_FAULT && b.fault==6);
        printf("Matching 6081 response at %u ms: phase=%u fault=%lu\n",delay,(unsigned)b.phase,(unsigned long)b.fault);
    }
    return 0;
}
