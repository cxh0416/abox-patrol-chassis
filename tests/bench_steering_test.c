#include "bench_steering.h"
#include "steering_angle.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static PatrolCanFrame reply(const BenchSteering* b, int32_t value)
{
    PatrolCanFrame f = PatrolCanopen_SdoWrite32(1,b->index,b->subindex,value);
    f.id = 0x581;
    f.dlc = b->read ? 4+b->width : 4;
    f.data[0] = b->read ? b->width==4 ? 0x43 : b->width==2 ? 0x4b : 0x4f : 0x60;
    return f;
}
static void advance(BenchSteering* b, uint32_t* now)
{
    PatrolCanFrame f,r;
    while (b->step==3 && b->tpdo_check<2) {
        assert(BenchSteering_Take(b,*now,&f));
        r=reply(b,b->expected);
        assert(BenchSteering_OnFrame(b,&r,*now+1));
        *now+=10;
    }
    BenchSteering_Tick(b,*now,1,1,1000,0);
    assert(BenchSteering_Take(b,*now,&f));
    assert(f.id==0 || f.id==0x601); /* never drive node */
    if (b->pending) {
        int32_t value=b->step==1 ? -111593 : b->step==2 ? -56037 : b->step==22 ? 0x21 : b->expected;
        r=reply(b,value);
        assert(BenchSteering_OnFrame(b,&r,*now+1));
    }
    *now+=10;
}
static void initial_limits(BenchSteering* b, int32_t low, int32_t high)
{
    PatrolCanFrame f,r;
    BenchSteering_Tick(b,0,1,1,0,0);
    assert(BenchSteering_Take(b,0,&f) && f.id==0);
    assert(BenchSteering_Take(b,1,&f) && b->index==0x201A && b->read);
    r=reply(b,low); assert(BenchSteering_OnFrame(b,&r,2));
    assert(BenchSteering_Take(b,3,&f) && b->index==0x2019 && b->read);
    r=reply(b,high); assert(BenchSteering_OnFrame(b,&r,4));
}
static void zero_limit_bootstrap(void)
{
    BenchSteering b; PatrolCanFrame f,r;
    BenchSteering_Init(&b);
    assert(BenchSteering_Start(&b,-1463,-1463,-1463,30000,0));
    initial_limits(&b,0,0);
    assert(b.phase==BENCH_PREPARING && b.fault==0);
    assert(b.minimum==-84142 && b.maximum==81216 && !b.limits_aligned);
    for (uint32_t t=5;t<9;t++) {
        assert(BenchSteering_Take(&b,t,&f));
        assert(f.id==0x601 && (b.index==0x201A || b.index==0x2019));
        r=reply(&b,b.expected); assert(BenchSteering_OnFrame(&b,&r,t));
    }
    assert(b.limits_aligned && b.cached_minimum==-84142 && b.cached_maximum==81216);
    /* A disappearing pair in a live epoch is not permission to re-anchor. */
    b.phase=BENCH_IDLE;
    assert(BenchSteering_Start(&b,-1463,-1463,-1463,30000,0));
    initial_limits(&b,0,0);
    assert(b.phase==BENCH_FAULT && b.fault==8 && b.cut_power);

    /* Commissioning keeps its original envelope, including rejecting zero. */
    BenchSteering_Init(&b);
    assert(BenchSteering_StartCommissioning(&b,-1463,-1463,60000,0));
    initial_limits(&b,0,0);
    assert(b.phase==BENCH_FAULT && b.fault==8);
    assert(!BenchSteering_Take(&b,5,&f));

    for (int kind=0;kind<3;kind++) {
        BenchSteering_Init(&b);
        assert(BenchSteering_Start(&b,-1463,-1463,-1463,30000,0));
        initial_limits(&b,kind==0?123:kind==1?100:0,kind==0?123:kind==1?-100:-1);
        assert(b.phase==BENCH_FAULT && b.fault==8);
    }
    /* An ACK is insufficient; mismatched limit readback cannot reach enable. */
    BenchSteering_Init(&b);
    assert(BenchSteering_Start(&b,-1463,-1463,-1463,30000,0));
    initial_limits(&b,0,0);
    assert(BenchSteering_Take(&b,5,&f) && !b.read);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,6));
    assert(BenchSteering_Take(&b,7,&f) && b.read);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,8));
    assert(b.phase==BENCH_FAULT && b.fault==10 && b.cut_power);

    BenchSteering_Init(&b);
    assert(BenchSteering_Start(&b,-1463,-1463,-1463,30000,0));
    BenchSteering_Tick(&b,0,1,0,0,0);
    assert(b.phase==BENCH_FAULT && !BenchSteering_Take(&b,1,&f));
}
int main(void)
{
    zero_limit_bootstrap();
    SteeringCoordinate coordinate;
    BenchSteering b; PatrolCanFrame f,r; uint32_t now=0; int32_t target;
    SteeringCoordinate_Reset(&coordinate);
    SteeringCoordinate_Observe(&coordinate,1,-83815,2945,3182,10,10);
    SteeringCoordinate_Observe(&coordinate,1,-83815,2945,3182,10,400);
    assert(!coordinate.valid); /* repeating old samples cannot establish a coordinate */
    SteeringCoordinate_Observe(&coordinate,1,-83815,2945,3182,410,410);
    SteeringCoordinate_Observe(&coordinate,1,-83815,2945,3182,420,420);
    assert(coordinate.valid);
    assert(coordinate.runtime_center==-72187);
    assert(SteeringAngle_FromControllerCount(-83815,coordinate.runtime_center,2710800,1)==-4289);
    SteeringCoordinate_Observe(&coordinate,1,0,0,3182,500,500);
    assert(coordinate.runtime_center==-72187); /* never re-zero an established coordinate */
    SteeringCoordinate_Reset(&coordinate);
    SteeringCoordinate_Observe(&coordinate,1,INT32_MIN,0,3182,0,0);
    SteeringCoordinate_Observe(&coordinate,1,INT32_MIN,0,3182,200,200);
    SteeringCoordinate_Observe(&coordinate,1,INT32_MIN,0,3182,400,400);
    assert(!coordinate.valid); /* runtime center overflow */
    assert(SteeringAngle_ToControllerCount(1000,-85123,2710800,1,-111593,-56037,&target)&&target==-82413);
    assert(SteeringAngle_ToControllerCount(-1000,-85123,2710800,1,-111593,-56037,&target)&&target==-87833);
    assert(!SteeringAngle_ToControllerCount(INT32_MAX,INT32_MAX,UINT32_MAX,-1,-5,5,&target));
    assert(!SteeringAngle_ToControllerCount(5000,-60000,2710800,1,-111593,-56037,&target));
    BenchSteering_Init(&b); assert(!BenchSteering_Take(&b,0,&f));
    assert(!BenchSteering_Start(&b,-83815,-85123,-85123,30001,now));
    assert(BenchSteering_Start(&b,-83815,-85123,-85123,30000,now));
    for(int i=0;i<20;i++) advance(&b,&now);
    assert(b.phase==BENCH_ACTIVE);
    for(int i=0;i<4;i++) advance(&b,&now);
    assert(b.applied==-82413 && b.step==16);
    /* Once 0x607A has been written, a newer remote target waits in the single
       desired slot until this trigger/clear cycle finishes. */
    now=b.last_trigger_ms+BENCH_STEERING_TRIGGER_INTERVAL_MS;
    BenchSteering_Tick(&b,now,1,1,500,0);
    assert(BenchSteering_Take(&b,now,&f) && b.index==0x607A);
    int32_t committed=b.applied;
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+1));
    BenchSteering_Tick(&b,now+2,1,1,0,0);
    assert(b.desired!=committed && b.applied==committed && b.step==18);
    assert(BenchSteering_Take(&b,now+2,&f) && b.index==0x6041 && b.read);
    r=reply(&b,0x27); assert(BenchSteering_OnFrame(&b,&r,now+3));
    assert(BenchSteering_Take(&b,now+4,&f) && b.index==0x6040 && f.data[4]==0x3F);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+5));
    assert(BenchSteering_Take(&b,now+6,&f) && b.index==0x6040 && f.data[4]==0x2F);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+7));
    assert(b.phase==BENCH_ACTIVE && b.step==16 && b.desired==b.center);

    /* Start-to-start trigger spacing is at least 100 ms. */
    BenchSteering_Tick(&b,now+8,1,1,0,0);
    assert(b.step==16 && !BenchSteering_Take(&b,now+8,&f));
    now=b.last_trigger_ms+BENCH_STEERING_TRIGGER_INTERVAL_MS;
    BenchSteering_Tick(&b,now,1,1,0,0);
    assert(BenchSteering_Take(&b,now,&f) && b.index==0x607A);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+1));
    assert(BenchSteering_Take(&b,now+2,&f) && b.index==0x6041 && b.read);
    r=reply(&b,0x27); assert(BenchSteering_OnFrame(&b,&r,now+3));
    assert(BenchSteering_Take(&b,now+4,&f) && b.index==0x6040 && f.data[4]==0x3F);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+5));
    assert(BenchSteering_Take(&b,now+6,&f) && b.index==0x6040 && f.data[4]==0x2F);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+7));
    assert(b.phase==BENCH_ACTIVE && b.step==16);
    BenchSteering_Tick(&b,now,0,1,0,0);
    assert(b.phase==BENCH_STOPPING);
    assert(BenchSteering_Take(&b,now,&f) && b.index==0x6040 && f.data[4]==6);
    r=reply(&b,0); BenchSteering_OnFrame(&b,&r,now+1);
    assert(BenchSteering_Take(&b,now+2,&f)&&b.index==0x6041);
    r=reply(&b,0x21); BenchSteering_OnFrame(&b,&r,now+3);
    assert(BenchSteering_Take(&b,now+4,&f)&&f.id==0&&f.data[0]==0x80);
    assert(b.phase==BENCH_IDLE); /* stop emitted no target position */
    assert(b.limits_aligned && b.minimum==-167802 && b.maximum==-2444);

    /* Current MAX/MID/CREEP profiles are equal, so runtime target changes do
       not require repeated 0x6081 writes. */
    BenchSteering_Init(&b); now=0;
    assert(BenchSteering_Start(&b,-83815,-85123,-85123,30000,now));
    for(int i=0;i<20;i++) advance(&b,&now);
    assert(b.phase==BENCH_ACTIVE && b.applied_profile_rpm==1000);
    now=b.last_trigger_ms+BENCH_STEERING_TRIGGER_INTERVAL_MS;
    BenchSteering_Tick(&b,now,1,1,30000,0);
    assert(BenchSteering_Take(&b,now,&f) && b.index==0x607A);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+5));
    assert(BenchSteering_Take(&b,now+1,&f) && b.index==0x6041 && b.read);
    r=reply(&b,0x27); assert(BenchSteering_OnFrame(&b,&r,now+2));
    assert(BenchSteering_Take(&b,now+3,&f) && b.index==0x6040 && f.data[4]==0x3F);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+4));
    assert(BenchSteering_Take(&b,now+5,&f) && b.index==0x6040 && f.data[4]==0x2F);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+6));
    assert(b.phase==BENCH_ACTIVE && b.step==16);
    BenchSteering_Tick(&b,now+7,1,1,30000,27000);
    assert(b.applied_profile_rpm==1000 && b.step==16);
    assert(!BenchSteering_Take(&b,now+7,&f));
    BenchSteering_Tick(&b,now+8,1,1,30000,24000);
    assert(b.applied_profile_rpm==1000 && b.step==16);
    assert(!BenchSteering_Take(&b,now+8,&f));

    BenchSteering_Init(&b);
    assert(BenchSteering_Start(&b,-83815,-72187,-85123,30000,0)); now=0;
    for(int i=0;i<20;i++) advance(&b,&now);
    assert(b.phase==BENCH_ACTIVE && b.minimum==-154866 && b.maximum==10492);
    /* Rearming in the same power epoch must verify, not shift, the existing limits. */
    b.phase=BENCH_IDLE;
    assert(BenchSteering_Start(&b,-83815,-72187,-85123,30000,now));
    advance(&b,&now); /* NMT */
    assert(BenchSteering_Take(&b,now,&f)); r=reply(&b,-154866);
    BenchSteering_OnFrame(&b,&r,now+1);
    assert(BenchSteering_Take(&b,now+2,&f)); r=reply(&b,10492);
    BenchSteering_OnFrame(&b,&r,now+3);
    assert(b.step==3 && b.minimum==-154866 && b.maximum==10492);

    /* Commissioning uses the controller's existing limits and never rewrites
       them around an uncalibrated provisional origin. */
    BenchSteering_Init(&b); now=0;
    assert(BenchSteering_StartCommissioning(&b,-83815,-83815,60000,now));
    advance(&b,&now); /* NMT */
    assert(BenchSteering_Take(&b,now,&f)); r=reply(&b,-111593);
    BenchSteering_OnFrame(&b,&r,now+1);
    assert(BenchSteering_Take(&b,now+2,&f)); r=reply(&b,-56037);
    BenchSteering_OnFrame(&b,&r,now+3);
    assert(b.step==3 && b.minimum==-111593 && b.maximum==-56037 && b.preserve_limits);

    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    advance(&b,&(uint32_t){0}); /* NMT */
    assert(BenchSteering_Take(&b,10,&f));
    r=reply(&b,-111593); r.dlc=5;
    BenchSteering_OnFrame(&b,&r,11); assert(b.pending);
    r=reply(&b,-111593); r.data[1]++;
    assert(!BenchSteering_OnFrame(&b,&r,12)&&b.pending);
    BenchSteering_Tick(&b,209,1,1,0,0); assert(b.pending&&b.phase!=BENCH_FAULT);
    BenchSteering_Tick(&b,210,1,1,0,0); assert(b.phase==BENCH_FAULT&&b.cut_power);
    assert(b.fault == 6U);
    BenchSteering_Stop(&b,211,1);
    assert(b.fault == 6U); /* emergency propagation preserves first timeout */
    assert(!BenchSteering_Take(&b,211,&f));
    /* Motion ACKs at 59ms complete normally; at 60ms the result must instead
       be read back. Verification has an 80ms read deadline followed by a
       bounded drain window ending at 200ms. */
    for (int expired=0;expired<2;expired++) {
        BenchSteering_Init(&b); b.phase=BENCH_ACTIVE; b.step=17;
        assert(BenchSteering_Take(&b,1000,&f)&&b.index==0x607A);
        r=reply(&b,0);
        assert(BenchSteering_OnFrame(&b,&r,expired?1060:1059));
        if (expired) {
            assert(b.phase==BENCH_ACTIVE&&b.verify&&!b.pending&&!b.cut_power);
            assert(BenchSteering_Take(&b,1060,&f)&&b.index==0x6041&&b.read);
            assert(f.data[0]==0x40); /* lost ACK never causes a target write replay */
            BenchSteering_Tick(&b,1139,1,1,0,0);
            assert(b.verify&&b.pending&&b.phase!=BENCH_FAULT);
            BenchSteering_Tick(&b,1140,1,1,0,0);
            assert(b.verify&&!b.pending&&b.phase!=BENCH_FAULT);
            r=reply(&b,0x2237);
            assert(BenchSteering_OnFrame(&b,&r,1150)); /* late reply is drained */
            assert(!BenchSteering_Take(&b,1150,&f));
            BenchSteering_Tick(&b,1259,1,1,0,0);
            assert(b.phase!=BENCH_FAULT);
            BenchSteering_Tick(&b,1260,1,1,0,0);
            assert(b.phase==BENCH_FAULT&&b.cut_power);
        }
        else assert(b.phase==BENCH_ACTIVE&&!b.pending&&!b.fault);
    }
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0); now=0;
    while (b.step != 4) advance(&b,&now);
    assert(BenchSteering_Take(&b,now,&f)); r=reply(&b,3);
    BenchSteering_OnFrame(&b,&r,now+1); assert(b.phase==BENCH_FAULT);
    assert(!BenchSteering_Take(&b,now+2,&f)); /* wrong mode never reaches enable */
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0); now=0;
    while (b.step != 11) advance(&b,&now);
    assert(BenchSteering_Take(&b,now,&f)); r=reply(&b,0x40);
    BenchSteering_OnFrame(&b,&r,now+1); assert(b.step==11 && !b.pending);
    assert(!BenchSteering_OnFrame(&b,&r,now+2)); /* duplicate cannot advance state */
    BenchSteering_Tick(&b,5000,1,1,1000,0); assert(b.phase==BENCH_FAULT);
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0); now=0;
    advance(&b,&now); BenchSteering_Take(&b,now,&f);
    r=reply(&b,0); r.data[0]=0x80; r.dlc=8; r.data[4]=1;
    BenchSteering_OnFrame(&b,&r,now+1); assert(b.phase==BENCH_FAULT&&b.cut_power);
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    BenchSteering_Tick(&b,1,1,0,0,0); assert(b.phase==BENCH_FAULT&&b.cut_power);
    /* PREPARING can be aborted through shutdown before limits are aligned. */
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    assert(b.phase==BENCH_PREPARING && !b.limits_aligned);
    BenchSteering_Stop(&b,10,0);
    assert(b.phase==BENCH_STOPPING && b.fault==0 && !b.cut_power);
    BenchSteering_Tick(&b,10,0,1,0,0);
    assert(BenchSteering_Take(&b,10,&f) && b.index==0x6040 && f.data[4]==6);
    r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,11));
    assert(BenchSteering_Take(&b,12,&f) && b.index==0x6041 && b.read);
    r=reply(&b,0x21); assert(BenchSteering_OnFrame(&b,&r,13));
    assert(BenchSteering_Take(&b,14,&f) && f.id==0 && f.data[0]==0x80);
    assert(b.phase==BENCH_IDLE && !b.cut_power);
    /* A shutdown that never completes still trips the bounded stop deadline. */
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    BenchSteering_Stop(&b,10,0); BenchSteering_Tick(&b,510,0,1,0,0);
    assert(b.phase==BENCH_FAULT&&b.cut_power);
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    BenchSteering_Tick(&b,1,1,1,1001,0); assert(b.phase==BENCH_FAULT);
    /* Field trace: disable confirmed at 181 ms; NMT serviced after 200 ms. */
    BenchSteering_Init(&b); b.phase=BENCH_ACTIVE;
    BenchSteering_Stop(&b,1000,0);
    assert(BenchSteering_Take(&b,1000,&f)); r=reply(&b,0);
    assert(BenchSteering_OnFrame(&b,&r,1001));
    assert(BenchSteering_Take(&b,1180,&f)); r=reply(&b,0x2231);
    assert(BenchSteering_OnFrame(&b,&r,1181));
    assert(BenchSteering_StopFinishReady(&b));
    BenchSteering_Tick(&b,1200,0,1,0,0);
    assert(b.phase==BENCH_STOPPING);
    assert(BenchSteering_Take(&b,1200,&f)&&f.id==0&&f.data[0]==0x80);
    assert(b.phase==BENCH_IDLE);
    BenchSteering_TxFailed(&b);
    assert(b.phase==BENCH_FAULT&&b.cut_power); /* NMT transmit failure still faults. */

    BenchSteering_Init(&b); b.phase=BENCH_ACTIVE;
    BenchSteering_Stop(&b,1000,0);
    assert(BenchSteering_Take(&b,1000,&f)); r=reply(&b,0);
    assert(BenchSteering_OnFrame(&b,&r,1001));
    assert(BenchSteering_Take(&b,1480,&f)); r=reply(&b,0x2231);
    assert(BenchSteering_OnFrame(&b,&r,1500));
    assert(b.phase==BENCH_FAULT&&b.fault==3); /* Late disable is not accepted. */

    BenchSteering_Init(&b); b.phase=BENCH_ACTIVE;
    BenchSteering_Stop(&b,1000,0);
    assert(BenchSteering_Take(&b,1000,&f)); r=reply(&b,0);
    assert(BenchSteering_OnFrame(&b,&r,1001));
    assert(BenchSteering_Take(&b,1180,&f)); r=reply(&b,0x2231);
    assert(BenchSteering_OnFrame(&b,&r,1181));
    BenchSteering_Tick(&b,1281,0,1,0,0);
    assert(b.phase==BENCH_FAULT&&b.cut_power); /* Housekeeping cannot hang forever. */
    /* Verified TPDO arriving at 192 ms confirms disable, including while an
       SDO reply is still outstanding. It must not release that transaction. */
    for (int pending=0;pending<2;pending++) {
        BenchSteering_Init(&b); b.phase=BENCH_ACTIVE; b.tpdo_check=2;
        BenchSteering_Stop(&b,1000,0);
        assert(BenchSteering_Take(&b,1000,&f)); r=reply(&b,0);
        assert(BenchSteering_OnFrame(&b,&r,1001));
        if (pending) assert(BenchSteering_Take(&b,1190,&f));
        r=(PatrolCanFrame){.id=0x181,.dlc=7,.data={0x31,0x22}};
        assert(BenchSteering_OnFrame(&b,&r,1192));
        assert(b.stop_confirmed && b.pending==pending);
        BenchSteering_Tick(&b,1200,0,1,0,0);
        assert(b.phase==BENCH_STOPPING);
        if (pending) {
            assert(!BenchSteering_Take(&b,1200,&f));
            r=reply(&b,0x2237); /* Older solicited status drained without undoing TPDO. */
            assert(BenchSteering_OnFrame(&b,&r,1201));
        }
        assert(BenchSteering_Take(&b,1210,&f)&&f.id==0);
    }
    for (int kind=0;kind<3;kind++) {
        BenchSteering_Init(&b); b.phase=BENCH_ACTIVE; b.tpdo_check=kind==0?0:2;
        BenchSteering_Stop(&b,1000,0);
        assert(BenchSteering_Take(&b,1000,&f)); r=reply(&b,0);
        assert(BenchSteering_OnFrame(&b,&r,1001));
        r=(PatrolCanFrame){.id=0x181,.dlc=kind==1?1:7,.data={0x31,0x22}};
        BenchSteering_OnFrame(&b,&r,kind==2?1500:1192);
        assert(!b.stop_confirmed); /* Unknown mapping, malformed or late PDO. */
        BenchSteering_Tick(&b,1500,0,1,0,0);
        assert(b.phase==BENCH_FAULT);
    }
    BenchSteering_Init(&b); b.phase=BENCH_PREPARING; b.step=3;
    assert(BenchSteering_Take(&b,0,&f)&&f.data[3]==1);
    r=reply(&b,0x80000181); /* Disabled COB-ID must not authorize PDO decoding. */
    BenchSteering_OnFrame(&b,&r,1); assert(b.phase==BENCH_FAULT);
    /* Vehicle trace: shutdown is acknowledged immediately, but PP deceleration
       keeps Operation Enabled beyond 200 ms. A 6-byte SDO status and verified
       TPDO confirm disable at 357 ms; they must precede the bounded deadline. */
    for (int pdo=0; pdo<2; ++pdo) {
        BenchSteering_Init(&b); b.phase=BENCH_ACTIVE; b.tpdo_check=2;
        BenchSteering_Stop(&b,1000,0);
        assert(BenchSteering_Take(&b,1000,&f)); r=reply(&b,0);
        assert(BenchSteering_OnFrame(&b,&r,1001));
        for (uint32_t t=1020; t<=1340; t+=40) {
            assert(BenchSteering_Take(&b,t,&f)); r=reply(&b,0x2237); r.dlc=6;
            assert(BenchSteering_OnFrame(&b,&r,t+1));
            BenchSteering_Tick(&b,t+2,0,1,0,0);
            assert(b.phase==BENCH_STOPPING && !b.stop_confirmed);
        }
        if (pdo) r=(PatrolCanFrame){.id=0x181,.dlc=7,.data={0x31,0x22}};
        else { assert(BenchSteering_Take(&b,1356,&f)); r=reply(&b,0x2231); r.dlc=6; }
        assert(BenchSteering_OnFrame(&b,&r,1357));
        assert(BenchSteering_StopFinishReady(&b));
        assert(BenchSteering_Take(&b,1360,&f) && f.id==0);
        assert(b.phase==BENCH_IDLE && !b.cut_power);
    }
    puts("PASS bench handshake, target conversion, limits, stop, malformed replies, faults and timeout");
}
