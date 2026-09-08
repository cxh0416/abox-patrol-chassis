#include "bench_drive.h"
#include <assert.h>
#include <stdio.h>

static void transact(BenchDrive* b, uint32_t* now)
{
    PatrolCanFrame f,r;
    assert(BenchDrive_Take(b,*now,&f));
    assert(f.id==0x602 || (f.id==0 && f.data[1]==2));
    if(b->pending) {
        r=PatrolCanopen_SdoWrite32(2,b->index,0,b->step==24?0x21:b->expected);
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
int main(void)
{
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
    BenchDrive_Tick(&b,now+2000,0,1,0,50,50,now,now); assert(b.phase==BENCH_FAULT && b.cut_power);
    active(&b,&now); BenchDrive_Tick(&b,now,1,1,51,0,0,now,now); assert(b.phase==BENCH_FAULT);
    active(&b,&now); BenchDrive_Tick(&b,now,1,0,0,0,0,now,now); assert(b.cut_power);
    BenchDrive_Init(&b); assert(BenchDrive_Start(&b,now)); transact(&b,&now);
    assert(BenchDrive_Take(&b,now,&f));
    r=f; r.id=0x581; r.data[0]=0x60; assert(!BenchDrive_OnFrame(&b,&r,now));
    r.id=0x582; r.dlc=3; assert(!BenchDrive_OnFrame(&b,&r,now));
    BenchDrive_Tick(&b,now+100,1,1,0,0,0,now,now); assert(b.cut_power);
    puts("bench drive tests passed");
}
