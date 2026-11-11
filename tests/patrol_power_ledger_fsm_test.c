#undef NDEBUG
#include "patrol_power_ledger_fsm.h"
#include "chassis_cloud_protocol.h"
#include "abox_mqtt_digest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t data[PATROL_POWER_WORKSPACE_SIZE]; size_t size; int exists; } File;
typedef struct {
    File files[2]; ABoxUfsRequest request; uint64_t op;
    uint8_t workspace[PATROL_POWER_WORKSPACE_SIZE];
    unsigned writes, cut; size_t prefix;
    int pending, marker, leased, blocked, actions, stops, power;
    uint64_t utc;
} Test;
static int start(void *p,const ABoxUfsRequest *r,uint64_t *op)
{ Test *t=p; assert(!t->pending); t->pending=1; t->request=*r; *op=++t->op; return 1; }
static ABoxAsyncStatus poll(void *p,uint64_t op,uint32_t *transferred)
{
    Test *t=p; assert(t->pending && op==t->op); t->pending=0;
    File *f=&t->files[strstr(t->request.path,"-b")?1:0];
    if(t->request.operation==ABOX_UFS_UPLOAD) {
        size_t n=t->request.size; if(++t->writes==t->cut)n=t->prefix;
        assert(n<PATROL_POWER_WORKSPACE_SIZE); f->exists=1; f->size=n; memcpy(f->data,t->request.data,n);
        *transferred=(uint32_t)n; return n==t->request.size?ABOX_ASYNC_OK:ABOX_ASYNC_ERROR;
    }
    if(!f->exists)return ABOX_ASYNC_NOT_FOUND;
    size_t n=f->size<t->request.size?f->size:t->request.size;
    memcpy(t->request.data,f->data,n); *transferred=(uint32_t)n;
    return n==t->request.size?ABOX_ASYNC_OK:ABOX_ASYNC_TRUNCATED;
}
static int cancel(void *p,uint64_t op) { Test *t=p; assert(op==t->op); t->pending=0; return 1; }
static int marked(void *p){return ((Test *)p)->marker;}
static int mark(void *p){((Test *)p)->marker=1;return 1;}
static int acquire(void *p){Test *t=p;if(t->leased||t->blocked)return 0;t->leased=1;return 1;}
static void release(void *p){Test *t=p;assert(t->leased);t->leased=0;}
static int admit(void *p){(void)p;return 1;}
static int on(void *p,uint8_t *out){Test *t=p;t->actions++;t->power=1;*out=1;return 1;}
static void stop(void *p){Test *t=p;t->stops++;t->power=0;}
static uint8_t power_state(void *p){return (uint8_t)((Test *)p)->power;}
static uint64_t utc(void *p){return ((Test *)p)->utc;}
static ABoxClockSnapshot clock_value(Test *t,uint64_t mono)
{ ABoxClockSnapshot c={0}; c.quality=ABOX_CLOCK_SYNCHRONIZED;c.utc_ms=t->utc;c.mono_ms=mono;c.error_ms=1;return c; }
static void boot(Test *t,PatrolPowerLedgerFsm *s,uint64_t mono)
{
    t->pending=t->leased=0;
    ABoxEc800UfsPort files={t,start,poll,cancel};
    PatrolPowerLedgerPort port={t,marked,mark,acquire,release,admit,on,stop,power_state,utc};
    assert(PatrolPowerLedgerFsm_Init(s,&files,&port,"UFS:test-a","UFS:test-b",t->workspace,sizeof(t->workspace)));
    assert(PatrolPowerLedgerFsm_Load(s,mono));
}
static void settle(Test *t,PatrolPowerLedgerFsm *s,uint64_t *mono)
{
    for(unsigned n=0;n<300;n++) {
        ABoxClockSnapshot c=clock_value(t,++*mono); PatrolPowerLedgerFsm_Poll(s,&c);
        if(!s->ready && ABoxUfsJournal_Status(&s->journal)<0)return;
        if(s->ready && !t->leased && s->active==PATROL_POWER_SLOTS)return;
    }
    assert(0 && "FSM did not settle");
}
static void baseline(void)
{
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t slot,other;
    const char payload[]="{\"version\":\"4.0\",\"timestamp\":1770186088333,\"data\":{\"requestId\":\"on1\",\"command\":\"power_on\",\"params\":null,\"expiresAt\":1770186093333}}";
    ChassisCloudCommand cmd; assert(ChassisCloud_Parse(payload,strlen(payload),&cmd)==CHASSIS_CLOUD_OK);
    uint8_t digest_a[32],digest_b[32],digest_changed[32];
    assert(ChassisCloud_RequestDigest(&cmd,digest_a));
    assert(ABoxMqttDigest_NullRequest("power_on",1,cmd.expires_at,digest_b));
    assert(ABoxMqttDigest_NullRequest("power_on",1,cmd.expires_at+1,digest_changed));
    assert(!memcmp(digest_a,digest_b,32) && memcmp(digest_a,digest_changed,32));
    boot(&t,&s,mono);settle(&t,&s,&mono); assert(t.marker==1);
    ABoxClockSnapshot c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A",cmd.request_id,1,cmd.expires_at,&c,&slot)==PATROL_POWER_NEW);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A",cmd.request_id,1,cmd.expires_at,&c,&other)==PATROL_POWER_WAIT);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A",cmd.request_id,1,cmd.expires_at+1,&c,&other)==PATROL_POWER_CONFLICT);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","another",1,cmd.expires_at,&c,&other)==PATROL_POWER_BUSY);
    settle(&t,&s,&mono);assert(t.actions==1);
    char first[256],again[256];int n=PatrolPowerLedgerFsm_Response(&s,slot,first,sizeof(first));assert(n>0);
    c=clock_value(&t,mono);assert(PatrolPowerLedgerFsm_Request(&s,"device-A","on1",1,cmd.expires_at,&c,&other)==PATROL_POWER_REPLAY);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","on1",0,cmd.expires_at,&c,&other)==PATROL_POWER_CONFLICT);
    boot(&t,&s,mono);settle(&t,&s,&mono);c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","on1",1,cmd.expires_at,&c,&other)==PATROL_POWER_REPLAY);
    assert(PatrolPowerLedgerFsm_Response(&s,other,again,sizeof(again))==n && !strcmp(first,again) && t.actions==1);
    for(unsigned i=2;i<=4;i++) { char id[8];snprintf(id,sizeof(id),"on%u",i);c=clock_value(&t,mono);
        assert(PatrolPowerLedgerFsm_Request(&s,"device-A",id,1,cmd.expires_at,&c,&slot)==PATROL_POWER_NEW);settle(&t,&s,&mono); }
    c=clock_value(&t,mono);assert(PatrolPowerLedgerFsm_Request(&s,"device-A","on5",1,cmd.expires_at,&c,&slot)==PATROL_POWER_FULL);
    t.utc+=700000;mono+=700000;c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","on5",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);settle(&t,&s,&mono);
    puts("PASS real request parser; duplicate, conflict, response replay, reset, capacity and protection window");
}
static void safety(void)
{
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t slot;
    boot(&t,&s,mono);settle(&t,&s,&mono);ABoxClockSnapshot c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","stopcase",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
    c=clock_value(&t,++mono);PatrolPowerLedgerFsm_Poll(&s,&c); /* Started reservation. */
    PatrolPowerLedgerFsm_SafeStop(&s);assert(t.stops==1);settle(&t,&s,&mono);assert(t.actions==0);
    char result[256];assert(PatrolPowerLedgerFsm_Response(&s,slot,result,sizeof(result)) && strstr(result,"superseded by safety stop"));
    c=clock_value(&t,mono);assert(PatrolPowerLedgerFsm_Request(&s,"device-A","late",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
    c=clock_value(&t,++mono);PatrolPowerLedgerFsm_Poll(&s,&c); /* reservation is in progress */
    t.utc+=5000;settle(&t,&s,&mono);assert(t.actions==0);
    assert(PatrolPowerLedgerFsm_Response(&s,slot,result,sizeof(result)) && strstr(result,"request expired"));
    c=clock_value(&t,mono); c.quality=ABOX_CLOCK_UNTRUSTED;
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","no-clock",1,t.utc+5000,&c,&slot)==PATROL_POWER_TIME_UNAVAILABLE);
    puts("PASS safety preemption, expiry after durable preparation and untrusted clock");
}
static void identity(void)
{
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t a,b;
    boot(&t,&s,mono);settle(&t,&s,&mono);ABoxClockSnapshot c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","same",1,t.utc+5000,&c,&a)==PATROL_POWER_NEW);settle(&t,&s,&mono);
    c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-B","same",1,t.utc+5000,&c,&b)==PATROL_POWER_NEW);settle(&t,&s,&mono);
    assert(a!=b && t.actions==2);boot(&t,&s,mono);settle(&t,&s,&mono);c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","same",1,t.utc+5000,&c,&a)==PATROL_POWER_REPLAY);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-B","same",1,t.utc+5000,&c,&b)==PATROL_POWER_REPLAY);
    assert(a!=b && t.actions==2);
    puts("PASS identity-scoped duplicate and recovery");
}
static void blocked_resource(void)
{
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t slot;
    boot(&t,&s,mono);settle(&t,&s,&mono);ABoxClockSnapshot c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","blocked",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
    t.blocked=1;
    for(unsigned i=0;i<10;i++){c=clock_value(&t,++mono);PatrolPowerLedgerFsm_Poll(&s,&c);}
    assert(!t.actions && !s.failure_pending);
    t.utc+=5000;c=clock_value(&t,++mono);PatrolPowerLedgerFsm_Poll(&s,&c);
    char device[32],id[65];uint16_t code=0;
    assert(PatrolPowerLedgerFsm_TakeFailure(&s,device,sizeof(device),id,sizeof(id),&code));
    assert(code==4005 && !strcmp(id,"blocked") && !strcmp(device,"device-A") && !t.actions);
    assert(!PatrolPowerLedgerFsm_TakeFailure(&s,device,sizeof(device),id,sizeof(id),&code));
    assert(s.ready && s.active==PATROL_POWER_SLOTS);
    puts("PASS resource contention ends in expiry without dispatch");
}
static void blocked_final(void)
{
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t slot;
    boot(&t,&s,mono);settle(&t,&s,&mono);ABoxClockSnapshot c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","final-block",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
    for(unsigned i=0;i<300 && !t.actions;i++) { c=clock_value(&t,++mono);PatrolPowerLedgerFsm_Poll(&s,&c); }
    assert(t.actions==1);t.blocked=1;mono+=60001;c=clock_value(&t,mono);PatrolPowerLedgerFsm_Poll(&s,&c);
    char device[32],id[65];uint16_t code;
    assert(PatrolPowerLedgerFsm_TakeFailure(&s,device,sizeof(device),id,sizeof(id),&code));
    assert(code==4003 && !s.ready && !strcmp(id,"final-block"));
    t.blocked=0;boot(&t,&s,mono);settle(&t,&s,&mono);c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","final-block",1,t.utc+5000,&c,&slot)==PATROL_POWER_FAILED);
    assert(t.actions==1);
    puts("PASS bounded final wait preserves uncertain reservation");
}
static void restored_protection(void)
{
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t slot;
    boot(&t,&s,mono);settle(&t,&s,&mono);ABoxClockSnapshot c=clock_value(&t,mono);
    for(unsigned i=0;i<PATROL_POWER_SLOTS;i++) {
        char id[8];snprintf(id,sizeof(id),"id%u",i);
        c=clock_value(&t,mono);
        assert(PatrolPowerLedgerFsm_Request(&s,"device-A",id,1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
        settle(&t,&s,&mono);
    }
    boot(&t,&s,mono);settle(&t,&s,&mono);
    /* Establish restored deadlines at the first synchronized snapshot. */
    c=clock_value(&t,++mono);PatrolPowerLedgerFsm_Poll(&s,&c);
    t.utc+=700000;c=clock_value(&t,++mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","extra",1,t.utc+5000,&c,&slot)==PATROL_POWER_FULL);
    mono+=700000;c=clock_value(&t,mono);
    assert(PatrolPowerLedgerFsm_Request(&s,"device-A","extra",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
    puts("PASS restored records retain monotonic protection after a UTC jump");
}
static void cuts(void)
{
    unsigned cases=0;
    for(unsigned cut=1;cut<=4;cut++)for(size_t prefix=0;prefix<PATROL_POWER_WORKSPACE_SIZE-1;prefix++) {
        Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;size_t slot;
        boot(&t,&s,mono);settle(&t,&s,&mono);t.writes=0;t.cut=cut;t.prefix=prefix;
        ABoxClockSnapshot c=clock_value(&t,mono);assert(PatrolPowerLedgerFsm_Request(&s,"device-A","cut",1,t.utc+5000,&c,&slot)==PATROL_POWER_NEW);
        settle(&t,&s,&mono);assert(!s.ready);t.cut=0;boot(&t,&s,mono);settle(&t,&s,&mono);c=clock_value(&t,mono);
        int match=PatrolPowerLedgerFsm_Request(&s,"device-A","cut",1,t.utc+5000,&c,&slot);
        if(t.actions) assert(match==PATROL_POWER_FAILED || match==PATROL_POWER_REPLAY);
        else assert(match==PATROL_POWER_NEW || match==PATROL_POWER_FAILED);
        assert(t.actions<=1);cases++;
    }
    Test t={0};PatrolPowerLedgerFsm s;uint64_t mono=0;t.utc=1770186088333ULL;t.marker=1;
    boot(&t,&s,mono);settle(&t,&s,&mono);assert(!s.ready);
    memset(&t,0,sizeof(t));t.utc=1770186088333ULL;t.marker=-1;
    boot(&t,&s,mono);settle(&t,&s,&mono);assert(!s.ready);
    printf("PASS %u torn-write cases, no accepted re-dispatch after effect; missing used media fail closed\n",cases);
}
int main(void){baseline();identity();safety();blocked_resource();blocked_final();restored_protection();cuts();return 0;}
