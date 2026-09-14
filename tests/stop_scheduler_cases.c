#include <assert.h>
#include <string.h>
#include "bench_steering.h"
#include "bench_drive.h"
#include "patrol_diagnostics.h"
static BenchSteering g_bench;
static BenchDrive g_drive;
static PatrolDiagnostics g_diagnostics;
static uint8_t g_bench_turn, g_drive_turn;
/* PRODUCTION_SCHEDULER */

static void reset_fixture(void)
{
    BenchSteering_Init(&g_bench); BenchDrive_Init(&g_drive);
    PatrolDiagnostics_Init(&g_diagnostics);
    g_diagnostics.steering_heartbeat_seen = g_diagnostics.drive_heartbeat_seen = 1;
    g_diagnostics.steering_heartbeat_state = g_diagnostics.drive_heartbeat_state = 5;
    g_diagnostics.steering_heartbeat_ms = g_diagnostics.drive_heartbeat_ms = 1000;
    g_diagnostics.steering_position_seen = g_diagnostics.left_rpm_seen = g_diagnostics.right_rpm_seen = 1;
    g_diagnostics.sdo_success_mask = 31;
    g_diagnostics.steering_status_ms = 990;
    g_diagnostics.drive_status_ms = 790; /* 210 ms old upon entering stop */
    g_diagnostics.steering_position_ms = 900;
    g_diagnostics.left_rpm_ms = 910; g_diagnostics.right_rpm_ms = 920;
    g_diagnostics.steering_status_word = 0x27; g_diagnostics.drive_status_word = 0x21;
    g_diagnostics.motion_priority = 1;
    g_bench.phase = BENCH_ACTIVE;
    BenchSteering_Stop(&g_bench, 1000, 0);
    g_bench_turn = 1; g_drive_turn = 0;
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
    if (!BenchSteering_OnFrame(&g_bench, &f, now))
        PatrolDiagnostics_OnCanFrame(&g_diagnostics, &f, now);
    else if (read && index == 0x6041 && g_bench.phase != BENCH_FAULT) {
        g_diagnostics.steering_status_ms = now;
        g_diagnostics.steering_status_word = f.data[4];
    }
}

int main(void)
{
    PatrolCanFrame f;
    reset_fixture();
    assert(take(1000, &f));
    assert(f.id == 0x601 && f.data[1] == 0x40 && f.data[2] == 0x60 && f.data[4] == 6);
    assert(!take(1001, &f)); /* no second owner while shutdown waits */
    f = PatrolCanopen_SdoRead(1, 0x6040, 0); f.data[0] = 0x2B;
    reply(f, 1005, 0);
    assert(take(1010, &f));
    assert(f.id == 0x602 && f.data[1] == 0x41 && f.data[2] == 0x60);
    reply(f, 1015, 0);
    for (uint32_t now=1020; now<1200 && g_bench.phase!=BENCH_IDLE; now+=10) {
        PatrolDiagnosticsSnapshot s;
        PatrolDiagnostics_Snapshot(&g_diagnostics, now, &s);
        assert(s.drive_status_fresh && s.steering_status_fresh &&
               s.drive_feedback_fresh && s.steering_feedback_fresh);
        BenchSteering_Tick(&g_bench, now, 0, 1, 0);
        assert(g_bench.phase != BENCH_FAULT);
        if (take(now, &f)) reply(f, now+5, 0);
    }
    assert(g_bench.phase == BENCH_IDLE && !g_bench.cut_power);
    /* A genuinely stuck shutdown still trips the original 200 ms deadline. */
    reset_fixture();
    for (uint32_t now=1000; now<=1200; now+=10) {
        BenchSteering_Tick(&g_bench, now, 0, 1, 0);
        if (g_bench.phase == BENCH_FAULT) break;
        if (take(now, &f)) reply(f, now+5, 1);
    }
    assert(g_bench.phase == BENCH_FAULT && g_bench.fault == 3 && g_bench.cut_power);
    /* Missing Node 2 responses still become stale, not artificially refreshed. */
    reset_fixture();
    assert(take(1000, &f)); reply(f, 1005, 0);
    assert(take(1010, &f));
    assert(!take(1011, &f));
    PatrolDiagnosticsSnapshot s;
    PatrolDiagnostics_Snapshot(&g_diagnostics, 1051, &s);
    assert(!s.drive_status_fresh);
    BenchSteering_Tick(&g_bench, 1051, 0, 0, 0);
    assert(g_bench.phase == BENCH_FAULT && g_bench.cut_power);
    return 0;
}
