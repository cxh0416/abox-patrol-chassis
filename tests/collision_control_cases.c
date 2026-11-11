#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chassis_control.h"
#include "bench_drive.h"

static void healthy(ChassisControl* c, uint32_t now)
{
    ChassisFeedback f = {0};
    f.calibrated = f.steering_online = f.drive_online = f.power_ready = 1;
    f.encoder_profile_ready = f.encoder_range_valid = 1;
    f.steering_actual_mdeg = 5000;
    f.feedback_ms = now;
    ChassisControl_SetFeedback(c, &f);
}

static void fixture(ChassisControl* c, uint32_t now)
{
    ChassisControl_Init(c);
    healthy(c, now);
    ChassisControl_CollisionInputs(c, 0, 1, now);
    assert(ChassisControl_Apply(c, "acquire", "phy-remote", 1, 1000, 800, 15000, now));
}

static void neutral(ChassisControl* c, uint32_t start)
{
    for (unsigned dt = 0; dt <= 1000; dt += 40) {
        ChassisControl_CollisionJoystick(c, 0, 0, start + dt, start + dt);
        assert(ChassisControl_CollisionBlocked(c));
    }
    ChassisControl_CollisionJoystick(c, 0, 0, start + 1040, start + 1040);
}

static void test_phases_and_directions(void)
{
    ChassisControl c;
    fixture(&c, 1000);
    ChassisControl_CollisionInputs(&c, CHASSIS_COLLISION_FRONT, 1, 1100);
    assert(c.target_rpm == 0 && c.requested_rpm == 0 && c.target_steering_mdeg == 5000);
    assert(c.enable_request && c.session_active && !c.fault_pending && !c.fault_latched);
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 2, 1000, 1000, -20000, 1200));
    assert(c.target_rpm == 0 && c.target_steering_mdeg == 5000 && c.enable_request);
    for (uint32_t t = 1100; t < 2100; t += 40)
        ChassisControl_CollisionJoystick(&c, 0, 0, t, t);
    assert((c.collision & CHASSIS_COLLISION_PHASE_MASK) == CHASSIS_COLLISION_DELAY);
    /* First second never contributes to the second, even when both contacts release early. */
    ChassisControl_CollisionInputs(&c, 0, 1, 1500);
    neutral(&c, 2100);
    assert(ChassisControl_CollisionBlocked(&c)); /* Real wheel stop confirmation still required. */
    ChassisControl_CollisionBrakeProcessed(&c, 1);
    assert(!ChassisControl_CollisionBlocked(&c));
    healthy(&c, 3200);
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 3, 1000, 700, 1000, 3200));
    assert(c.target_rpm == 700);

    ChassisControl_CollisionInputs(&c, CHASSIS_COLLISION_FRONT, 1, 3300);
    neutral(&c, 4300);
    ChassisControl_CollisionBrakeProcessed(&c, 1);
    healthy(&c, 5400);
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 4, 1000, 700, 0, 5400));
    assert(c.target_rpm == 0);
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 5, 1000, -700, 0, 5400));
    assert(c.reversal_pending && c.target_rpm == 0); /* Retained forward direction cannot be bypassed. */
    for (unsigned i = 1; i <= 3; ++i) { healthy(&c, 5400 + 40*i); ChassisControl_Tick(&c, 5400 + 40*i); }
    assert(!c.reversal_pending && c.target_rpm == -700);
    ChassisControl_CollisionInputs(&c, 3, 1, 5600); /* Rear newly presses while front remains pressed. */
    assert(c.collision & CHASSIS_COLLISION_PENDING);
    neutral(&c, 6600);
    ChassisControl_CollisionBrakeProcessed(&c, 1);
    healthy(&c, 7700);
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 6, 1000, -700, 0, 7700));
    assert(c.target_rpm == 0);
    ChassisControl_CollisionInputs(&c, CHASSIS_COLLISION_REAR, 1, 7720);
    assert(!(c.collision & CHASSIS_COLLISION_PENDING)); /* Release does not trigger a new brake. */
    c.active_direction = 0;
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 7, 1000, 700, 0, 7720));
    assert(c.target_rpm == 700);
    assert(ChassisControl_Apply(&c, "renew", "phy-remote", 8, 1000, -700, 0, 7720));
    assert(c.target_rpm == 0 && c.enable_request && !c.fault_latched);
}

static void test_jitter_missing_and_wrap(void)
{
    ChassisControl c;
    fixture(&c, 1000);
    ChassisControl_CollisionInputs(&c, 1, 1, 1100);
    ChassisControl_CollisionBrakeProcessed(&c, 1);
    ChassisControl_CollisionJoystick(&c, 0, 0, 1090, 1110); /* Queued before impact. */
    assert((c.collision & CHASSIS_COLLISION_PHASE_MASK) == CHASSIS_COLLISION_DELAY);
    ChassisControl_CollisionJoystick(&c, 10, -10, 2100, 2100);
    assert((c.collision & CHASSIS_COLLISION_PHASE_MASK) == CHASSIS_COLLISION_NEUTRAL_TIMING);
    ChassisControl_CollisionJoystick(&c, 11, 0, 2140, 2140);
    assert((c.collision & CHASSIS_COLLISION_PHASE_MASK) == CHASSIS_COLLISION_WAIT_NEUTRAL);
    ChassisControl_CollisionJoystick(&c, 0, 0, 2180, 2180);
    ChassisControl_CollisionJoystick(&c, 0, 0, 2420, 2420); /* Gap restarts the full second. */
    assert(c.reversal_started_ms == 2420);
    c.lease_deadline_ms = 5000;
    healthy(&c, 2530); ChassisControl_Tick(&c, 2530);
    assert((c.collision & CHASSIS_COLLISION_PHASE_MASK) == CHASSIS_COLLISION_WAIT_NEUTRAL);
    neutral(&c, 2540);
    assert(!ChassisControl_CollisionBlocked(&c));
    /* Missing board validity cannot be bypassed by a neutral handset. */
    ChassisControl_CollisionInputs(&c, 0, 0, 4000);
    neutral(&c, 5000); ChassisControl_CollisionBrakeProcessed(&c, 1);
    assert(ChassisControl_CollisionBlocked(&c));
    ChassisControl_CollisionInputs(&c, 0, 1, 6100);
    assert(!ChassisControl_CollisionBlocked(&c));
    fixture(&c, UINT32_MAX - 600);
    ChassisControl_CollisionInputs(&c, 1, 1, UINT32_MAX - 500);
    neutral(&c, 499);
    ChassisControl_CollisionBrakeProcessed(&c, 1);
    assert(!ChassisControl_CollisionBlocked(&c));
    ChassisControl_CollisionInputs(&c, 1, 1, 1700);
    assert(!ChassisControl_CollisionBlocked(&c)); /* Held input never repeats the event. */
}

static void ack(BenchDrive* b, PatrolCanFrame* f, uint32_t now)
{
    PatrolCanFrame r = {0x582,8,{0x60,0,0,0,0,0,0,0}};
    r.data[1] = f->data[1]; r.data[2] = f->data[2];
    assert(BenchDrive_OnFrame(b, &r, now));
}

static void active(BenchDrive* b)
{
    BenchDrive_Init(b); b->phase = BENCH_ACTIVE; b->step = 12;
    b->direction = 1; b->applied = b->applied_left = b->applied_right = 800;
    b->desired = b->writing = 800;
}

static void test_enabled_zero_and_inflight_pair(void)
{
    BenchDrive b; PatrolCanFrame f;
    active(&b);
    /* One nonzero left target has already been sent. Its old right half must never follow. */
    b.step = 13; assert(BenchDrive_Take(&b, 1000, &f));
    BenchDrive_CollisionHold(&b, 1010);
    ack(&b, &f, 1020);
    BenchDrive_Tick(&b, 1030, 1, 1, 0, 100, 100, 1030, 1030);
    assert(BenchDrive_Take(&b, 1030, &f));
    assert(f.data[1] == 0xff && f.data[2] == 0x60);
    for (int j = 4; j < 8; ++j) assert(f.data[j] == 0);
    ack(&b, &f, 1040);
    assert(BenchDrive_Take(&b, 1050, &f));
    assert(f.data[1] == 0xff && f.data[2] == 0x68);
    for (int j = 4; j < 8; ++j) assert(f.data[j] == 0);
    ack(&b, &f, 1060);
    assert(!BenchDrive_CollisionStopped(&b));
    for (unsigned i = 1; i <= 3; ++i) {
        uint32_t t = 1060 + i*40;
        BenchDrive_Tick(&b, t, 1, 1, 0, 0, 0, t, t);
        if (i < 3) assert(!BenchDrive_CollisionStopped(&b));
    }
    assert(BenchDrive_CollisionStopped(&b));
    assert(b.phase == BENCH_ACTIVE && !b.cut_power && !b.fault);
    for (uint32_t t = 1200; t < 4000; t += 40) {
        BenchDrive_Tick(&b, t, 1, 1, 0, 0, 0, t, t);
        assert(!BenchDrive_Take(&b, t, &f)); /* No repeated brake, shutdown or enable writes. */
    }
    active(&b); BenchDrive_CollisionHold(&b, 1000);
    BenchDrive_Tick(&b, 7000, 1, 1, 0, 100, 100, 7000, 7000);
    assert(b.phase == BENCH_FAULT); /* A genuine failure to stop retains protection. */
}

static void steering_ack(BenchSteering* b, const PatrolCanFrame* f, uint32_t now)
{
    PatrolCanFrame r={0x581,8,{0x60,0,0,0,0,0,0,0}};
    r.data[1]=f->data[1]; r.data[2]=f->data[2];
    assert(BenchSteering_OnFrame(b,&r,now));
}

static void test_steering_supersedes_old_target(void)
{
    BenchSteering b; PatrolCanFrame f;
    BenchSteering_Init(&b); b.phase=BENCH_ACTIVE; b.step=17;
    b.minimum=-90000; b.maximum=90000; b.limit_mdeg=30000;
    b.desired=27108; b.applied_profile_rpm=1000; b.pp_ack_low=1;
    assert(BenchSteering_Take(&b,1000,&f)); /* Old target has been sent, its trigger has not. */
    BenchSteering_CollisionHold(&b);
    steering_ack(&b,&f,1010);
    assert(b.step==16 && b.phase==BENCH_ACTIVE);
    BenchSteering_Tick(&b,1020,1,1,0,0);
    assert(BenchSteering_Take(&b,1020,&f));
    assert(f.data[1]==0x7a && f.data[2]==0x60);
    for (int i=4;i<8;i++) assert(f.data[i]==0); /* Latest hold, never the old 10-degree trigger. */
    steering_ack(&b,&f,1030);
    b.step=18; b.pp_ack_low=1;
    assert(BenchSteering_Take(&b,1040,&f));
    assert(f.data[4]==0x3f);
    BenchSteering_CollisionHold(&b);
    steering_ack(&b,&f,1050);
    assert(b.step==19); /* Already-sent trigger must be cleared first. */
    assert(BenchSteering_Take(&b,1060,&f) && f.data[4]==0x2f);
    steering_ack(&b,&f,1070);
    assert(b.step==16 && b.phase==BENCH_ACTIVE && !b.cut_power && !b.fault);
}

int main(void)
{
    test_phases_and_directions();
    test_jitter_missing_and_wrap();
    test_enabled_zero_and_inflight_pair();
    test_steering_supersedes_old_target();
    puts("PASS collision phases, direction interlock, fresh neutral, wrap, enabled zero and stop failure");
    return 0;
}
