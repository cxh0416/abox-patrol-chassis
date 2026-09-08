#include "bench_steering.h"
#include "steering_angle.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static PatrolCanFrame reply(const BenchSteering* b, int32_t value)
{
    PatrolCanFrame f = PatrolCanopen_SdoWrite32(1,b->index,0,value);
    f.id = 0x581;
    f.dlc = b->read ? 4+b->width : 4;
    f.data[0] = b->read ? b->width==4 ? 0x43 : b->width==2 ? 0x4b : 0x4f : 0x60;
    return f;
}
static void advance(BenchSteering* b, uint32_t* now)
{
    PatrolCanFrame f,r;
    BenchSteering_Tick(b,*now,1,1,1000);
    assert(BenchSteering_Take(b,*now,&f));
    assert(f.id==0 || f.id==0x601); /* never drive node */
    if (b->pending) {
        int32_t value=b->step==1 ? -111593 : b->step==2 ? -56037 : b->step==22 ? 0x21 : b->expected;
        r=reply(b,value);
        assert(BenchSteering_OnFrame(b,&r,*now+1));
    }
    *now+=10;
}
int main(void)
{
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
    SteeringCoordinate_Observe(&coordinate,1,INT32_MAX,0,3182,0,0);
    SteeringCoordinate_Observe(&coordinate,1,INT32_MAX,0,3182,200,200);
    SteeringCoordinate_Observe(&coordinate,1,INT32_MAX,0,3182,400,400);
    assert(!coordinate.valid); /* runtime center overflow */
    assert(SteeringAngle_ToControllerCount(1000,-85123,2710800,1,-111593,-56037,&target)&&target==-82413);
    assert(SteeringAngle_ToControllerCount(-1000,-85123,2710800,1,-111593,-56037,&target)&&target==-87833);
    assert(!SteeringAngle_ToControllerCount(INT32_MAX,INT32_MAX,UINT32_MAX,-1,-5,5,&target));
    assert(!SteeringAngle_ToControllerCount(5000,-60000,2710800,1,-111593,-56037,&target));
    BenchSteering_Init(&b); assert(!BenchSteering_Take(&b,0,&f));
    assert(BenchSteering_Start(&b,-83815,-85123,-85123,1000,now));
    for(int i=0;i<20;i++) advance(&b,&now);
    assert(b.phase==BENCH_ACTIVE);
    for(int i=0;i<3;i++) advance(&b,&now);
    assert(b.applied==-82413 && b.step==16);
    BenchSteering_Tick(&b,now,0,1,0);
    assert(b.phase==BENCH_STOPPING);
    assert(BenchSteering_Take(&b,now,&f) && b.index==0x6040 && f.data[4]==6);
    r=reply(&b,0); BenchSteering_OnFrame(&b,&r,now+1);
    assert(BenchSteering_Take(&b,now+2,&f)&&b.index==0x6041);
    r=reply(&b,0x21); BenchSteering_OnFrame(&b,&r,now+3);
    assert(BenchSteering_Take(&b,now+4,&f)&&f.id==0&&f.data[0]==0x80);
    assert(b.phase==BENCH_IDLE); /* stop emitted no target position */
    assert(b.limits_aligned && b.minimum==-98677 && b.maximum==-71569);

    BenchSteering_Init(&b);
    assert(BenchSteering_Start(&b,-83815,-72187,-85123,1000,0)); now=0;
    for(int i=0;i<20;i++) advance(&b,&now);
    assert(b.phase==BENCH_ACTIVE && b.minimum==-85741 && b.maximum==-58633);
    /* Rearming in the same power epoch must verify, not shift, the existing limits. */
    b.phase=BENCH_IDLE;
    assert(BenchSteering_Start(&b,-83815,-72187,-85123,1000,now));
    advance(&b,&now); /* NMT */
    assert(BenchSteering_Take(&b,now,&f)); r=reply(&b,-85741);
    BenchSteering_OnFrame(&b,&r,now+1);
    assert(BenchSteering_Take(&b,now+2,&f)); r=reply(&b,-58633);
    BenchSteering_OnFrame(&b,&r,now+3);
    assert(b.step==3 && b.minimum==-85741 && b.maximum==-58633);

    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    advance(&b,&(uint32_t){0}); /* NMT */
    assert(BenchSteering_Take(&b,10,&f));
    r=reply(&b,-111593); r.dlc=5;
    BenchSteering_OnFrame(&b,&r,11); assert(b.pending);
    r=reply(&b,-111593); r.data[1]++;
    assert(!BenchSteering_OnFrame(&b,&r,12)&&b.pending);
    BenchSteering_Tick(&b,111,1,1,0); assert(b.phase==BENCH_FAULT&&b.cut_power);
    assert(!BenchSteering_Take(&b,112,&f));
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0); now=0;
    advance(&b,&now); BenchSteering_Take(&b,now,&f);
    r=reply(&b,0); r.data[0]=0x80; r.dlc=8; r.data[4]=1;
    BenchSteering_OnFrame(&b,&r,now+1); assert(b.phase==BENCH_FAULT&&b.cut_power);
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    BenchSteering_Tick(&b,1,1,0,0); assert(b.phase==BENCH_FAULT&&b.cut_power);
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    BenchSteering_Stop(&b,10,0); BenchSteering_Tick(&b,210,0,1,0);
    assert(b.phase==BENCH_FAULT&&b.cut_power);
    BenchSteering_Init(&b); BenchSteering_Start(&b,-83815,-85123,-85123,1000,0);
    BenchSteering_Tick(&b,1,1,1,1001); assert(b.phase==BENCH_FAULT);
    puts("PASS bench handshake, target conversion, limits, stop, malformed replies, faults and timeout");
}
