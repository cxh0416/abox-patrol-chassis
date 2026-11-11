#include "patrol_power_ledger_fsm.h"
#include "abox_mqtt_v4.h"
#include "abox_mqtt_request.h"
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(PatrolPowerLedgerEntry)==124, "entry layout");
enum { EMPTY, PREPARING, PREPARED, FINALIZING, FINAL, INTERRUPTED };
enum { OUT_OK=1, OUT_UNAVAILABLE, OUT_FAILED, OUT_EXPIRED, OUT_NO_TIME, OUT_CANCELLED };
enum { LOADING, IDLE, NEED_PREPARE, WRITING_PREPARE, NEED_FINAL, WRITING_FINAL, REJECTED, FAILED };
#define PAYLOAD 501U
static void put64(uint8_t *p,uint64_t n) { for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8U*i)); }
static uint64_t get64(const uint8_t *p) { uint64_t n=0; for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(8U*i); return n; }
static int decode(void *v,const uint8_t *p,size_t size)
{
    PatrolPowerLedgerFsm *s=v;
    if(!p && !size) { memset(s->entry,0,sizeof(s->entry)); return 1; }
    if(!p || size!=PAYLOAD || memcmp(p,"PF03",4)) return 0;
    for(size_t i=0;i<PATROL_POWER_SLOTS;i++) {
        const uint8_t *q=p+37U+116U*i; unsigned st=q[0];
        if(st>2 || (st && (!memchr(q+1,0,65) || !ABoxMqttV4_IdValid((const char *)q+1,ABOX_MQTT_ID_REQUEST) ||
             !get64(q+66) || get64(q+66)>9007199254740991ULL ||
             !memchr(q+84,0,32) || !ABoxMqttV4_DeviceIdValid((const char *)q+84))) ||
           (st==1 && (get64(q+74)||q[82]||q[83])) ||
           (st==2 && (q[82]<OUT_OK||q[82]>OUT_CANCELLED||q[83]>1 || get64(q+74)>9007199254740991ULL))) return 0;
        if(st) for(size_t j=0;j<i;j++) {
            const uint8_t *old=p+37U+116U*j;
            if(old[0] && !strcmp((const char *)old+1,(const char *)q+1) &&
                !strcmp((const char *)old+84,(const char *)q+84)) return 0;
        }
    }
    memset(s->entry,0,sizeof(s->entry));
    for(size_t i=0;i<PATROL_POWER_SLOTS;i++) {
        const uint8_t *q=p+37U+116U*i; PatrolPowerLedgerEntry *e=&s->entry[i];
        if(!q[0]) continue;
        memcpy(e->id,q+1,65); e->expires=get64(q+66); e->response_time=get64(q+74);
        e->outcome=q[82]; e->power_on=q[83]; memcpy(e->device,q+84,32);
        /* Establish a new monotonic protection deadline after trusted time
         * becomes available; never evict on a later UTC jump alone. */
        e->protect_mono=UINT64_MAX;
        e->state=q[0]==1 ? INTERRUPTED : FINAL;
    }
    return 1;
}
static int encode(void *v,uint8_t *p,size_t cap,size_t *size)
{
    PatrolPowerLedgerFsm *s=v; if(cap<PAYLOAD) return 0;
    memset(p,0,PAYLOAD); memcpy(p,"PF03",4);
    for(size_t i=0;i<PATROL_POWER_SLOTS;i++) {
        PatrolPowerLedgerEntry *e=&s->entry[i]; uint8_t *q=p+37U+116U*i;
        if(!e->state) continue;
        q[0]=(e->state==FINAL || e->state==FINALIZING) ? 2 : 1;
        memcpy(q+1,e->id,65); put64(q+66,e->expires); memcpy(q+84,e->device,32);
        if(q[0]==2) { put64(q+74,e->response_time); q[82]=e->outcome; q[83]=e->power_on; }
    }
    *size=PAYLOAD; return 1;
}
static int marked(void *v) { PatrolPowerLedgerFsm *s=v; return s->port.marker_read(s->port.context); }
static int mark(void *v) { PatrolPowerLedgerFsm *s=v; return s->port.marker_write(s->port.context); }
static int acquire(void *v) { PatrolPowerLedgerFsm *s=v; return s->port.acquire(s->port.context); }
static void release(void *v) { PatrolPowerLedgerFsm *s=v; s->port.release(s->port.context); }
int PatrolPowerLedgerFsm_Init(PatrolPowerLedgerFsm *s,const ABoxEc800UfsPort *files,const PatrolPowerLedgerPort *p,const char *a,const char *b,uint8_t *workspace,size_t workspace_size)
{
    if(!s||!p||!p->marker_read||!p->marker_write||!p->acquire||!p->release||!p->admit||!p->act_on||!p->safe_stop||!p->power_state||!p->utc_now) return 0;
    memset(s,0,sizeof(*s)); s->port=*p; s->phase=LOADING; s->active=PATROL_POWER_SLOTS;
    ABoxUfsJournalPort jp={s,decode,encode,marked,mark,acquire,release};
    return workspace_size==PATROL_POWER_WORKSPACE_SIZE && ABoxUfsJournal_Init(&s->journal,files,&jp,a,b,workspace,workspace_size);
}
int PatrolPowerLedgerFsm_Load(PatrolPowerLedgerFsm *s,uint64_t mono)
{ if(!s||s->phase!=LOADING)return 0; s->now_mono=mono; return ABoxUfsJournal_Load(&s->journal,(uint32_t)mono); }
int PatrolPowerLedgerFsm_Busy(const PatrolPowerLedgerFsm *s)
{ return s && (s->phase==NEED_PREPARE || s->phase==WRITING_PREPARE || s->phase==NEED_FINAL || s->phase==WRITING_FINAL); }
void PatrolPowerLedgerFsm_SafeStop(PatrolPowerLedgerFsm *s)
{ if(!s)return; s->port.safe_stop(s->port.context); ++s->stop_epoch; if(s->phase==NEED_PREPARE||s->phase==WRITING_PREPARE)s->cancelled=1; }
int PatrolPowerLedgerFsm_Request(PatrolPowerLedgerFsm *s,const char *device,const char *id,int on,uint64_t expiry,const ABoxClockSnapshot *clock,size_t *index)
{
    if(!s||!device||strlen(device)>=32U||!ABoxMqttV4_DeviceIdValid(device)||!id||!index||!clock||!ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_REQUEST)) return PATROL_POWER_INVALID;
    if(!s->ready || s->phase==FAILED) return PATROL_POWER_FAILED;
    for(size_t i=0;i<PATROL_POWER_SLOTS;i++) {
        PatrolPowerLedgerEntry *e=&s->entry[i]; if(!e->state || strcmp(id,e->id) || strcmp(device,e->device))continue;
        *index=i; if(!on || expiry!=e->expires) return PATROL_POWER_CONFLICT;
        if(e->state==FINAL) return PATROL_POWER_REPLAY;
        return e->state==INTERRUPTED ? PATROL_POWER_FAILED : PATROL_POWER_WAIT;
    }
    if(!on||!expiry||expiry>9007199254740991ULL) return PATROL_POWER_INVALID;
    if(s->phase!=IDLE) return PATROL_POWER_BUSY;
    if(!s->port.admit(s->port.context)) return PATROL_POWER_BUSY;
    uint16_t code=ABoxMqttV4_CheckExpiry(clock,expiry);
    if(code!=200) return code==4005 ? PATROL_POWER_EXPIRED : PATROL_POWER_TIME_UNAVAILABLE;
    if(clock->error_ms>ABOX_CLOCK_ERROR_LIMIT_MS || expiry-clock->utc_ms>7500U)
        return PATROL_POWER_TIME_UNAVAILABLE;
    for(size_t i=0;i<PATROL_POWER_SLOTS;i++) {
        PatrolPowerLedgerEntry *e=&s->entry[i]; uint64_t until=ABoxMqttV4_IdempotencyProtectUntil(e->expires);
        if(e->state && !((e->state==FINAL||e->state==INTERRUPTED) && clock->mono_ms>=e->protect_mono &&
            clock->utc_ms>=clock->error_ms && clock->utc_ms-clock->error_ms>until)) continue;
        memset(e,0,sizeof(*e)); strcpy(e->id,id); strcpy(e->device,device); e->expires=expiry; e->state=PREPARING;
        until=ABoxMqttV4_IdempotencyProtectUntil(expiry);
        uint64_t lower=clock->utc_ms>clock->error_ms ? clock->utc_ms-clock->error_ms : 0;
        uint64_t remaining=until>lower ? until-lower : 0;
        e->protect_mono=UINT64_MAX-clock->mono_ms<remaining ? UINT64_MAX : clock->mono_ms+remaining;
        s->active=(uint8_t)i; s->cancelled=0; s->admitted_epoch=s->stop_epoch; s->phase=NEED_PREPARE;
        *index=i; return PATROL_POWER_NEW;
    }
    return PATROL_POWER_FULL;
}
void PatrolPowerLedgerFsm_Poll(PatrolPowerLedgerFsm *s,const ABoxClockSnapshot *clock)
{
    if(!s||!clock||s->phase==FAILED)return;
    s->now_mono=clock->mono_ms;
    ABoxUfsJournal_Poll(&s->journal,(uint32_t)clock->mono_ms);
    int status=ABoxUfsJournal_Status(&s->journal);
    if(status<0) {
        if(s->active<PATROL_POWER_SLOTS) { s->failure_pending=1; s->failure_code=4003; }
        s->ready=0; s->phase=FAILED; return;
    }
    if(s->phase==LOADING) { if(status>0) { s->ready=1; s->phase=IDLE; } return; }
    if(clock->quality==ABOX_CLOCK_SYNCHRONIZED) {
        for(size_t i=0;i<PATROL_POWER_SLOTS;i++) {
            PatrolPowerLedgerEntry *e=&s->entry[i];
            if(e->state && e->protect_mono==UINT64_MAX) {
                uint64_t until=ABoxMqttV4_IdempotencyProtectUntil(e->expires);
                uint64_t lower=clock->utc_ms>clock->error_ms ? clock->utc_ms-clock->error_ms : 0;
                uint64_t remaining=until>lower ? until-lower : 0;
                e->protect_mono=UINT64_MAX-clock->mono_ms<remaining ? UINT64_MAX : clock->mono_ms+remaining;
            }
        }
    }
    if(s->phase==NEED_PREPARE) {
        uint16_t code=ABoxMqttV4_CheckExpiry(clock,s->entry[s->active].expires);
        if(s->cancelled || code!=200) {
            s->failure_pending=1;s->failure_code=s->cancelled?4003:code;s->phase=REJECTED;return;
        }
    }
    if(s->phase==NEED_FINAL && clock->mono_ms>=s->final_wait_started &&
       clock->mono_ms-s->final_wait_started>=60000U) {
        /* The reservation remains durable; never repeat an action whose
         * final response could not be committed. */
        s->failure_pending=1;s->failure_code=4003;s->ready=0;s->phase=FAILED;return;
    }
    if(s->phase==NEED_PREPARE||s->phase==NEED_FINAL) {
        int started=ABoxUfsJournal_Save(&s->journal,(uint32_t)clock->mono_ms);
        if(started<0) { s->failure_pending=1;s->failure_code=4003;s->ready=0;s->phase=FAILED; }
        else if(started>0) s->phase=s->phase==NEED_PREPARE?WRITING_PREPARE:WRITING_FINAL;
        return;
    }
    if(!status) return;
    PatrolPowerLedgerEntry *e=&s->entry[s->active];
    if(s->phase==WRITING_PREPARE) {
        e->state=PREPARED; uint8_t power=s->port.power_state(s->port.context);
        uint16_t code=ABoxMqttV4_CheckExpiry(clock,e->expires);
        if(s->cancelled || s->admitted_epoch!=s->stop_epoch) e->outcome=OUT_CANCELLED;
        else if(code==4005) e->outcome=OUT_EXPIRED;
        else if(code!=200) e->outcome=OUT_NO_TIME;
        else { int r=s->port.act_on(s->port.context,&power);
            e->outcome=r>0?OUT_OK:r==0?OUT_UNAVAILABLE:r==-2?OUT_CANCELLED:OUT_FAILED; }
        e->power_on=power; e->response_time=s->port.utc_now(s->port.context);
        if(e->response_time>9007199254740991ULL) e->response_time=0;
        e->state=FINALIZING; s->final_wait_started=clock->mono_ms; s->phase=NEED_FINAL; return;
    }
    if(s->phase==WRITING_FINAL) { e->state=FINAL; s->phase=IDLE; s->active=PATROL_POWER_SLOTS; }
}
int PatrolPowerLedgerFsm_TakeFailure(PatrolPowerLedgerFsm *s,char *device,size_t dcap,char *id,size_t icap,uint16_t *code)
{
    if(!s||!s->failure_pending||s->active>=PATROL_POWER_SLOTS||!device||!id||!code)return 0;
    PatrolPowerLedgerEntry *e=&s->entry[s->active];
    if(strlen(e->device)>=dcap || strlen(e->id)>=icap)return 0;
    strcpy(device,e->device);strcpy(id,e->id);*code=s->failure_code;s->failure_pending=0;
    if(s->phase==REJECTED) { memset(e,0,sizeof(*e));s->active=PATROL_POWER_SLOTS;s->phase=IDLE; }
    return 1;
}
int PatrolPowerLedgerFsm_Response(PatrolPowerLedgerFsm *s,size_t i,char *out,size_t cap)
{
    if(!s||!s->ready||i>=PATROL_POWER_SLOTS||s->entry[i].state!=FINAL||!out||!cap)return 0;
    PatrolPowerLedgerEntry *e=&s->entry[i]; char stamp[21];
    if(!ABoxMqttV4_FormatInteger(stamp,sizeof(stamp),e->response_time))return 0;
    const char *msg; unsigned code;
    switch(e->outcome) {
    case OUT_OK: code=200; msg="operation accepted"; break;
    case OUT_UNAVAILABLE: code=5001; msg="power output unavailable"; break;
    case OUT_FAILED: code=5001; msg="power output write failed"; break;
    case OUT_EXPIRED: code=4005; msg="request expired"; break;
    case OUT_NO_TIME: code=4003; msg="trusted time unavailable"; break;
    case OUT_CANCELLED: code=4003; msg="superseded by safety stop"; break;
    default:return 0;
    }
    int n=snprintf(out,cap,"{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"result\":{\"powerOutputOn\":%s}}}",stamp,e->id,code,msg,e->power_on?"true":"false");
    return n>0 && (size_t)n<cap ? n : 0;
}
