#include "bench_drive.h"
#include <stdlib.h>
#include <string.h>

static void fail(BenchDrive* b, uint32_t fault)
{ if(b->phase==BENCH_FAULT) return; b->pending=0; b->fault=fault; b->cut_power=1; b->phase=BENCH_FAULT; }
static int8_t direction(int16_t rpm) { return (rpm>0)-(rpm<0); }
void BenchDrive_Init(BenchDrive* b) { memset(b,0,sizeof(*b)); }
uint8_t BenchDrive_Start(BenchDrive* b, uint32_t now)
{
    if(b->phase!=BENCH_IDLE) return 0;
    BenchDrive_Init(b); b->phase=BENCH_PREPARING; b->started_ms=now; return 1;
}
void BenchDrive_Stop(BenchDrive* b, uint32_t now, uint8_t emergency)
{
    if(emergency) { fail(b,1); return; }
    if(b->phase==BENCH_IDLE || b->phase==BENCH_FAULT || b->phase==BENCH_STOPPING) return;
    /* Cancel pending motion; no write is retried after a lost acknowledgement. */
    b->pending=0; b->phase=BENCH_STOPPING; b->started_ms=now;
    b->step=20; b->desired=0; b->confirmations=0; b->reversing=0;
}
void BenchDrive_TxFailed(BenchDrive* b) { fail(b,2); }
void BenchDrive_Tick(BenchDrive* b, uint32_t now, uint8_t requested, uint8_t safe,
    int16_t rpm, int32_t left, int32_t right, uint32_t left_ms, uint32_t right_ms)
{
    if(b->phase==BENCH_IDLE || b->phase==BENCH_FAULT) return;
    if(!safe || abs(rpm)>BENCH_DRIVE_MAX_RPM) { fail(b,3); return; }
    if(!requested) BenchDrive_Stop(b,now,0);
    if(b->pending && now-b->sent_ms>=100U) { fail(b,4); return; }
    if(b->phase==BENCH_PREPARING && now-b->started_ms>=5000U) { fail(b,5); return; }
    if(b->phase==BENCH_STOPPING && now-b->started_ms>=2000U) { fail(b,6); return; }
    if(b->phase!=BENCH_STOPPING) b->desired=rpm;
    if(b->phase==BENCH_ACTIVE && b->step==12) {
        if(b->reversing) {
            if(now-b->started_ms>=2000U) { fail(b,6); return; }
        } else if(direction(rpm) && b->direction && direction(rpm)!=b->direction) {
            b->reversing=1; b->started_ms=now; b->confirmations=0;
            b->step=13; b->writing=0; /* Always send both zero targets first. */
        } else if(rpm!=b->applied) { b->writing=rpm; b->step=13; }
    }
    if((b->reversing && b->step==12) || (b->phase==BENCH_STOPPING && b->step==22)) {
        /* Each count requires BOTH wheels sampled after zero ACK and after the previous pair. */
        if((int32_t)(left_ms-b->zero_ms)>0 && (int32_t)(right_ms-b->zero_ms)>0 &&
           (int32_t)(left_ms-b->left_ms)>0 && (int32_t)(right_ms-b->right_ms)>0) {
            b->left_ms=left_ms; b->right_ms=right_ms;
            if(llabs(left)<=20 && llabs(right)<=20) b->confirmations++;
            else b->confirmations=0;
            if(b->confirmations>=3) {
                if(b->phase==BENCH_STOPPING) b->step=23;
                else { b->reversing=0; b->direction=0; b->writing=b->desired; b->step=13; }
            }
        }
    }
}
uint8_t BenchDrive_Take(BenchDrive* b, uint32_t now, PatrolCanFrame* f)
{
    int32_t value=0;
    if(b->pending || b->phase==BENCH_IDLE || b->phase==BENCH_FAULT || b->step==12 || b->step==22) return 0;
    if(b->step==0 || b->step==25) {
        /* Vendor dual-drive examples select Pre-Operational for SDO control.
         * No PDO mapping has been approved; do not start unsolicited PDO traffic. */
        memset(f,0,sizeof(*f)); f->dlc=2; f->data[0]=0x80; f->data[1]=2;
        if(b->step==25) b->phase=BENCH_IDLE; else b->step++;
        return 1;
    }
    b->read=0; b->width=4;
    switch(b->step) {
    case 1: b->index=0x6060; b->width=1; value=3; break;
    case 2: b->index=0x6061; b->width=1; b->read=1; b->expected=3; break;
    case 3: case 4: b->index=b->step==3?0x60FF:0x68FF; break;
    case 5: case 7: case 9: b->index=0x6040; b->width=2; value=b->step==5?6:b->step==7?7:15; break;
    /* VSY dual: bits 0/1/2 are Ready/Switched/Enabled (vendor PDF p24).
     * Observed Ready=0x1001; unlike Node 1, bit 5 is not set here. */
    case 6: case 8: case 10: b->index=0x6041; b->width=2; b->read=1; b->expected=b->step==6?1:b->step==8?3:7; break;
    case 13: case 14: b->index=b->step==13?0x60FF:0x68FF; value=b->writing; break;
    case 20: case 21: b->index=b->step==20?0x60FF:0x68FF; break;
    case 23: b->index=0x6040; b->width=2; value=6; break;
    case 24: b->index=0x6041; b->width=2; b->read=1; break;
    default: fail(b,7); return 0;
    }
    *f=b->read?PatrolCanopen_SdoRead(2,b->index,0):b->width==1?
        PatrolCanopen_SdoWrite8(2,b->index,0,(uint8_t)value):b->width==2?
        PatrolCanopen_SdoWrite16(2,b->index,0,(uint16_t)value):PatrolCanopen_SdoWrite32(2,b->index,0,value);
    b->pending=1; b->sent_ms=now; return 1;
}
uint8_t BenchDrive_OnFrame(BenchDrive* b, const PatrolCanFrame* f, uint32_t now)
{
    int32_t value; uint8_t width;
    if(!b->pending || f->id!=0x582 || f->dlc<4 || f->dlc>8 ||
       f->data[1]!=(uint8_t)b->index || f->data[2]!=(uint8_t)(b->index>>8) || f->data[3]) return 0;
    if(now-b->sent_ms>=100U) { fail(b,4); return 1; }
    if(f->data[0]==0x80) { if(f->dlc==8) fail(b,8); return 1; }
    if(b->read) {
        if(!PatrolCanopen_ParseSdoValue(f,2,b->index,0,&value,&width) || width!=b->width) return 1;
        if(b->index==0x6041) {
            if(value&8) { fail(b,9); return 1; }
            if(b->step==24 ? (value&4)!=0 : (value&0x4F)!=b->expected) { b->pending=0; return 1; }
        } else if(value!=b->expected) { fail(b,10); return 1; }
    } else if(f->data[0]!=0x60) return 1;
    b->pending=0;
    if(b->step==10) { b->step=12; b->phase=BENCH_ACTIVE; }
    else if(b->step==14) {
        b->step=12; b->applied=b->writing;
        if(b->writing) b->direction=direction(b->writing);
        if(b->reversing) { b->zero_ms=now; b->left_ms=now; b->right_ms=now; }
    } else {
        b->step++;
        if(b->step==22) { b->zero_ms=now; b->left_ms=now; b->right_ms=now; }
    }
    return 1;
}
