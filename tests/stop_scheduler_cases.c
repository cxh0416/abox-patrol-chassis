#include <assert.h>
#include <string.h>
#include "bench_steering.h"
#include "bench_drive.h"
#include "patrol_diagnostics.h"
#define CENTER_HOST_REAL_DIAGNOSTICS
#include "center_runtime_stubs.h"

static uint8_t take(uint32_t now, PatrolCanFrame* out)
{
    uint8_t owner;
    return PatrolRuntime_Take(now, out, &owner);
}

static void reset_fixture(void)
{
    BenchSteering_Init(&g_motion.bench); BenchDrive_Init(&g_motion.drive);
    PatrolDiagnostics_Init(&g_motion.diagnostics);
    g_motion.diagnostics.steering_heartbeat_seen = g_motion.diagnostics.drive_heartbeat_seen = 1;
    g_motion.diagnostics.steering_heartbeat_state = g_motion.diagnostics.drive_heartbeat_state = 5;
    g_motion.diagnostics.steering_heartbeat_ms = g_motion.diagnostics.drive_heartbeat_ms = 1000;
    g_motion.diagnostics.steering_position_seen = g_motion.diagnostics.left_rpm_seen = g_motion.diagnostics.right_rpm_seen = 1;
    g_motion.diagnostics.sdo_success_mask = 31;
    g_motion.diagnostics.steering_status_ms = 990;
    g_motion.diagnostics.drive_status_ms = 790; /* 210 ms old upon entering stop */
    g_motion.diagnostics.steering_position_ms = 900;
    g_motion.diagnostics.left_rpm_ms = 910; g_motion.diagnostics.right_rpm_ms = 920;
    g_motion.diagnostics.steering_status_word = 0x27; g_motion.diagnostics.drive_status_word = 0x21;
    g_motion.diagnostics.motion_priority = 1;
    g_motion.bench.phase = BENCH_ACTIVE;
    BenchSteering_Stop(&g_motion.bench, 1000, 0);
    g_motion.bench_turn = 1; g_motion.drive_turn = 0;
}

static void reply(PatrolCanFrame f, uint32_t now, uint8_t never_disable)
{
    uint16_t index = f.data[1] | (uint16_t)f.data[2] << 8;
    if (f.id == 0) return;
    uint8_t read = f.data[0] == 0x40;
    f.id -= 0x80;
    f.data[0] = read ? (index == 0x6041 ? 0x4B : 0x43) : 0x60;
    memset(f.data + 4, 0, 4);
    if (read && index == 0x6041) f.data[4] = (f.id == 0x581 && (never_disable || now < 1100)) ? 0x27 : 0x21;
    PatrolRuntime_OnCanFrame(&f, now);
}

int main(void)
{
    PatrolCanFrame f;
    /* Near-expiry feedback wins the next CAN slot over queued motion writes. */
    reset_fixture();
    g_motion.bench.phase = BENCH_ACTIVE;
    g_motion.bench.step = 16U;
    g_motion.drive.phase = BENCH_ACTIVE;
    g_motion.drive.step = 12U;
    g_motion.bench_turn = g_motion.drive_turn = 1U;
    assert(take(1000, &f));
    assert(f.id == 0x602 && f.data[0] == 0x40 && f.data[1] == 0x41 && f.data[2] == 0x60);
    /*
     * A timed-out low-priority diagnostic transaction must release the next
     * bus slot to the urgent feedback read, not to a queued actuator write.
     */
    reset_fixture();
    g_motion.bench.phase = BENCH_ACTIVE;
    g_motion.bench.step = 16U;
    g_motion.drive.phase = BENCH_ACTIVE;
    g_motion.drive.step = 12U;
    g_motion.bench_turn = g_motion.drive_turn = 1U;
    g_motion.diagnostics.pending_valid = 1U;
    g_motion.diagnostics.pending_entry = 5U;
    g_motion.diagnostics.pending_sent_ms = 1000U;
    assert(!take(1059, &f));
    assert(take(1061, &f));
    assert(f.id == 0x602 && f.data[0] == 0x40 && f.data[1] == 0x41 && f.data[2] == 0x60);
    reset_fixture();
    assert(take(1000, &f));
    assert(f.id == 0x601 && f.data[1] == 0x40 && f.data[2] == 0x60 && f.data[4] == 6);
    assert(!take(1001, &f)); /* no second owner while shutdown waits */
    f = PatrolCanopen_SdoRead(1, 0x6040, 0); f.data[0] = 0x2B;
    reply(f, 1005, 0);
    assert(take(1010, &f));
    assert(f.id == 0x602 && f.data[1] == 0x41 && f.data[2] == 0x60);
    reply(f, 1015, 0);
    for (uint32_t now=1020; now<1200 && g_motion.bench.phase!=BENCH_IDLE; now+=10) {
        PatrolDiagnosticsSnapshot s;
        PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &s);
        assert(s.drive_status_fresh && s.steering_status_fresh &&
               s.drive_feedback_fresh && s.steering_feedback_fresh);
        BenchSteering_Tick(&g_motion.bench, now, 0, 1, 0, 0);
        assert(g_motion.bench.phase != BENCH_FAULT);
        if (take(now, &f)) reply(f, now+5, 0);
    }
    assert(g_motion.bench.phase == BENCH_IDLE && !g_motion.bench.cut_power);
    /* Disable confirmation near the deadline must prioritize NMT over a due
       diagnostic read, including when the alternator points to diagnostics. */
    reset_fixture();
    assert(take(1000, &f)); reply(f, 1001, 0);
    assert(take(1010, &f)); reply(f, 1011, 0);
    assert(take(1180, &f)); reply(f, 1181, 0);
    assert(BenchSteering_StopFinishReady(&g_motion.bench));
    assert(!g_motion.bench_turn);
    BenchSteering_Tick(&g_motion.bench, 1200, 0, 1, 0, 0);
    assert(take(1200, &f));
    assert(f.id == 0 && f.data[0] == 0x80 && g_motion.bench.phase == BENCH_IDLE);
    /* A genuinely stuck shutdown still trips the bounded 500 ms deadline. */
    reset_fixture();
    for (uint32_t now=1000; now<=1500; now+=10) {
        BenchSteering_Tick(&g_motion.bench, now, 0, 1, 0, 0);
        if (g_motion.bench.phase == BENCH_FAULT) break;
        if (take(now, &f)) reply(f, now+5, 1);
    }
    assert(g_motion.bench.phase == BENCH_FAULT && g_motion.bench.fault == 3 && g_motion.bench.cut_power);
    /* Missing Node 2 responses still become stale, not artificially refreshed. */
    reset_fixture();
    assert(take(1000, &f)); reply(f, 1005, 0);
    assert(take(1010, &f));
    assert(!take(1011, &f));
    PatrolDiagnosticsSnapshot s;
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, 1051, &s);
    assert(!s.drive_status_fresh);
    BenchSteering_Tick(&g_motion.bench, 1051, 0, 0, 0, 0);
    assert(g_motion.bench.phase == BENCH_FAULT && g_motion.bench.cut_power);
    /*
     * A single temporarily stale feedback source must first request the
     * normal bounded stop sequence, not classify the drive actuator as failed.
     */
    reset_fixture();
    mock_power = 1U;
    mock_can_ready = 1U;
    mock_now = 1000U;
    g_motion.coordinate.valid = 1U;
    g_motion.bench.phase = BENCH_ACTIVE;
    g_motion.bench.step = 16U;
    g_motion.drive.phase = BENCH_ACTIVE;
    g_motion.drive.step = 12U;
    g_motion.feedback_gate_mask = 16U;
    mock_control.feedback.calibrated = 1U;
    mock_control.feedback.encoder_profile_ready = 1U;
    mock_control.feedback.encoder_range_valid = 1U;
    mock_control.feedback.steering_online = 1U;
    mock_control.feedback.drive_online = 1U;
    mock_control.feedback.power_ready = 0U;
    mock_control.feedback.feedback_ms = mock_now;
    mock_control.session_active = mock_control.enable_request = 1U;
    mock_control.lease_deadline_ms = mock_now + 1000U;
    PatrolRuntime_Tick();
    assert(g_motion.bench.phase == BENCH_STOPPING);
    assert(g_motion.drive.phase == BENCH_STOPPING);
    assert(!g_motion.bench.cut_power && !g_motion.drive.cut_power);
    assert(!mock_control.fault_latched && !mock_control.fault_pending);
    /*
     * The first tick revokes the remote session while the bounded stop is
     * still active. Subsequent ticks must keep the same stop classification,
     * otherwise the now-inactive session would turn the same stale feedback
     * into an immediate actuator fault.
     */
    assert(!mock_control.session_active);
    mock_now += 10U;
    PatrolRuntime_Tick();
    assert(g_motion.bench.phase == BENCH_STOPPING);
    assert(g_motion.drive.phase == BENCH_STOPPING);
    assert(!g_motion.bench.cut_power && !g_motion.drive.cut_power);
    assert(!mock_control.fault_latched && !mock_control.fault_pending);
    /* Once feedback is fresh, the same stop still completes without latching. */
    g_motion.feedback_gate_mask = 0U;
    mock_control.feedback.power_ready = 1U;
    mock_now += 10U;
    PatrolRuntime_Tick();
    assert(g_motion.bench.phase == BENCH_STOPPING);
    assert(g_motion.drive.phase == BENCH_STOPPING);
    assert(!g_motion.bench.cut_power && !g_motion.drive.cut_power);
    assert(!mock_control.fault_latched && !mock_control.fault_pending);
    /* Reset cannot energize outputs and must preserve the original evidence. */
    PatrolRuntime_Init(); mock_now=2000;
    g_motion.diagnostics.drive_status_word=0xBF;
    g_motion.diagnostics.drive_code_ms[0]=1950;
    g_motion.diagnostics.drive_code_ms[1]=0;
    PatrolRuntime_CaptureFault(1,13,4096);
    MotionFaultRecord before,after;
    assert(PatrolRuntime_GetFault(&before));
    assert(before.drive_sw==0xBF && before.left_code_age==50 && before.right_code_age==UINT32_MAX);
    mock_power=1; assert(!PatrolRuntime_ResetFault());
    mock_power=0; mock_ota=1; assert(!PatrolRuntime_ResetFault()); mock_ota=0;
    PatrolRuntime_Tick();
    assert(PatrolRuntime_FaultStorage()==1);
    assert(PatrolRuntime_ResetFault());
    assert(!mock_power && !mock_control.fault_latched && !mock_control.enable_request && !mock_permit);
    assert(PatrolRuntime_GetFault(&after) && memcmp(&before,&after,sizeof(before))==0);
    return 0;
}
