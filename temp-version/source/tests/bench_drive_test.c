#include "bench_drive.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void transact(BenchDrive* b, uint32_t* now)
{
    PatrolCanFrame f,r; uint8_t step=b->step;
    assert(BenchDrive_Take(b,*now,&f));
    assert(f.id==0x602 || (f.id==0 && f.data[1]==2));
    if(step==13 || step==14) {
        int32_t commanded=(int32_t)((uint32_t)f.data[4] | ((uint32_t)f.data[5]<<8) |
            ((uint32_t)f.data[6]<<16) | ((uint32_t)f.data[7]<<24));
        int percent=step==13 ? -b->writing_percent : b->writing_percent;
        int32_t magnitude=percent>0?(int32_t)b->writing*percent/100:b->writing;
        assert(commanded==(step==13?magnitude:-magnitude));
    }
    if(b->pending) {
        int32_t expected=b->step==2?3:b->step==6?1:b->step==8?3:b->step==10?7:0x21;
        r=PatrolCanopen_SdoWrite32(2,b->index,0,expected);
        r.id=0x582; r.dlc=b->read?4+b->width:4;
        r.data[0]=b->read?(b->width==1?0x4f:0x4b):0x60;
        assert(BenchDrive_OnFrame(b,&r,*now+1));
    }
    *now+=10;
}
static void active(BenchDrive* b,uint32_t* now)
{
    BenchDrive_Init(b); assert(BenchDrive_Start(b,*now));
    while(b->phase==BENCH_PREPARING) transact(b,now);
    assert(b->phase==BENCH_ACTIVE && b->applied==0);
}

static void test_differential(void)
{
    const int16_t speeds[] = {0,1,10,500,1000,2000};
    const int32_t angles[] = {-30000,-11000,-10000,-1001,-1000,0,1000,1001,10000,11000,30000};
    BenchDrive b; PatrolCanFrame f; uint32_t now=10;
    for (unsigned i=0;i<sizeof(speeds)/sizeof(speeds[0]);++i)
        for (unsigned j=0;j<sizeof(angles)/sizeof(angles[0]);++j)
            for (int sign=-1;sign<=1;sign+=2) {
                active(&b,&now);
                int16_t rpm=(int16_t)(sign*speeds[i]); int32_t angle=angles[j];
                int percent=abs(angle)>=11000?65:90;
                int16_t inner=(int16_t)((int32_t)rpm*percent/100);
                int16_t left=angle < -1000?inner:rpm, right=angle > 1000?inner:rpm;
                BenchDrive_SetSteeringAngle(&b,angle);
                BenchDrive_Tick(&b,now,1,1,rpm,0,0,now,now);
                if (rpm) { transact(&b,&now); transact(&b,&now); }
                assert(b.applied_left==left && b.applied_right==right && b.applied==rpm);
                BenchDrive_Tick(&b,now,1,1,rpm,0,0,now,now);
                assert(!BenchDrive_Take(&b,now,&f)); /* No periodic redundant writes. */
            }
    /* Hysteresis remembers the selected band on both sides of 10 degrees. */
    const int32_t boundary[] = {10000,10999,11000,10000,9001,9000,10000,1000,-1001};
    const int percent[] = {90,90,65,65,65,90,90,100,90};
    active(&b,&now);
    for (unsigned i=0;i<sizeof(boundary)/sizeof(boundary[0]);++i) {
        BenchDrive_SetSteeringAngle(&b,boundary[i]);
        BenchDrive_Tick(&b,now,1,1,2000,0,0,now,now);
        if (b.step==13) { transact(&b,&now); transact(&b,&now); }
        int16_t inner=(int16_t)(2000*percent[i]/100);
        assert(b.applied_left==(boundary[i]<-1000?inner:2000));
        assert(b.applied_right==(boundary[i]>1000?inner:2000));
    }
    /* An angle change after the left ACK cannot replace/starve the frozen right target. */
    active(&b,&now); BenchDrive_SetSteeringAngle(&b,-20000);
    BenchDrive_Tick(&b,now,1,1,2000,0,0,now,now);
    transact(&b,&now); assert(b.applied_left==1300 && b.direction==1);
    BenchDrive_SetSteeringAngle(&b,20000);
    BenchDrive_Tick(&b,now,1,1,2000,0,0,now,now);
    transact(&b,&now); assert(b.applied_right==2000);
    BenchDrive_Tick(&b,now,1,1,2000,0,0,now,now);
    transact(&b,&now); transact(&b,&now);
    assert(b.applied_left==2000 && b.applied_right==1300);
    /* Reversing after only the first wheel moved still requires both zero writes. */
    active(&b,&now); BenchDrive_SetSteeringAngle(&b,-20000);
    BenchDrive_Tick(&b,now,1,1,2000,0,0,now,now); transact(&b,&now);
    BenchDrive_Tick(&b,now,1,1,-2000,1300,0,now,now);
    assert(!BenchDrive_Take(&b,now,&f));
    BenchDrive_Tick(&b,now,1,1,-2000,1300,0,now,now);
    assert(b.reversing && b.writing==0 && b.writing_percent==0);
    transact(&b,&now); transact(&b,&now);
    for (int i=0;i<3;++i) {
        now+=20; BenchDrive_Tick(&b,now,1,1,-2000,0,0,now,now);
    }
    assert(!b.reversing && b.writing==-2000 && b.writing_percent==-65);
    transact(&b,&now); transact(&b,&now);
    BenchDrive_Stop(&b,now,0); transact(&b,&now); transact(&b,&now);
    assert(!b.applied_left && !b.applied_right && b.step==22);
    puts("differential directions, ratios, deadband, hysteresis, pair freeze and reversal passed");
}
int main(void)
{
    test_differential();
    BenchDrive b; uint32_t now=10; PatrolCanFrame f,r;
    active(&b,&now);
    BenchDrive_Tick(&b,now,1,1,30,0,0,now,now);
    transact(&b,&now); transact(&b,&now); assert(b.applied==30);
    BenchDrive_Tick(&b,now,1,1,-30,30,30,now,now);
    assert(b.reversing && b.writing==0);
    transact(&b,&now); transact(&b,&now); assert(b.applied==0);
    for(int i=0;i<6;i++) BenchDrive_Tick(&b,now+i,1,1,-30,0,0,b.zero_ms,b.zero_ms);
    assert(b.confirmations==0);
    for(int i=0;i<3;i++) {
        now+=20;
        BenchDrive_Tick(&b,now,1,1,-30,0,0,now,b.zero_ms);
    }
    assert(b.confirmations==0); /* One wheel cannot count as a pair. */
    for(int i=0;i<2;i++) { now+=20; BenchDrive_Tick(&b,now,1,1,-30,0,0,now,now); }
    assert(b.reversing && b.applied==0);
    now+=20; BenchDrive_Tick(&b,now,1,1,-30,0,21,now,now); assert(b.confirmations==0);
    for(int i=0;i<3;i++) { now+=20; BenchDrive_Tick(&b,now,1,1,-30,0,0,now,now); }
    assert(!b.reversing && b.writing==-30);
    transact(&b,&now); transact(&b,&now); assert(b.applied==-30);
    BenchDrive_Stop(&b,now,0); transact(&b,&now); transact(&b,&now);
    assert(b.step==22);
    for(int i=0;i<3;i++) { now+=20; BenchDrive_Tick(&b,now,0,1,0,0,0,now,now); }
    transact(&b,&now); transact(&b,&now); transact(&b,&now); assert(b.phase==BENCH_IDLE);
    active(&b,&now); BenchDrive_Stop(&b,now,0);
    BenchDrive_Tick(&b,now+2000,0,1,0,50,50,now,now);
    assert(b.phase==BENCH_STOPPING && !b.cut_power);
    BenchDrive_Tick(&b,now+5999,0,1,0,50,50,now,now);
    assert(b.phase==BENCH_STOPPING && !b.cut_power);
    BenchDrive_Tick(&b,now+6000,0,1,0,50,50,now,now);
    assert(b.phase==BENCH_FAULT && b.cut_power);
    /* Full-scale targets reach both motor SDOs with the correct wheel signs. */
    for (int sign=-1; sign<=1; sign+=2) {
        active(&b,&now); BenchDrive_Tick(&b,now,1,1,sign*2000,0,0,now,now);
        transact(&b,&now); transact(&b,&now); assert(b.applied==sign*2000);
        BenchDrive_Stop(&b,now,1);
        active(&b,&now); BenchDrive_Tick(&b,now,1,1,sign*2001,0,0,now,now);
        assert(b.phase==BENCH_FAULT && b.cut_power);
    }
    active(&b,&now); BenchDrive_Tick(&b,now,1,0,0,0,0,now,now); assert(b.cut_power);
    BenchDrive_Init(&b); assert(BenchDrive_Start(&b,now)); transact(&b,&now);
    assert(BenchDrive_Take(&b,now,&f));
    r=f; r.id=0x581; r.data[0]=0x60; assert(!BenchDrive_OnFrame(&b,&r,now));
    r.id=0x582; r.dlc=3; assert(!BenchDrive_OnFrame(&b,&r,now));
    BenchDrive_Tick(&b,now+100,1,1,0,0,0,now,now); assert(b.cut_power);
    puts("bench drive tests passed");
}
