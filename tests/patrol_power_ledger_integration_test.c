#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "patrol_power_ledger.h"
#include "patrol_power_ledger_fsm.h"
#include "abox_ec800_ufs_at.h"
#include "cloud_internal.h"
#include "time_base.h"

CloudContext g_cloud;
static uint64_t mono,utc=1770186088333ULL,operation;
static uint8_t ota,provisioning,ota_pending,enrollment_busy,marker,power,ready=1,leased_request;
static uint32_t generation,actions;
static char published[1536];
static struct { uint8_t bytes[PATROL_POWER_WORKSPACE_SIZE]; uint32_t size; int exists; } files[2];
static ABoxUfsRequest request;
static int file_start(void *context,const ABoxUfsRequest *r,uint64_t *op)
{ (void)context;assert(!leased_request);leased_request=1;request=*r;*op=++operation;return 1; }
static ABoxAsyncStatus file_poll(void *context,uint64_t op,uint32_t *size)
{
    (void)context;assert(leased_request && op==operation);leased_request=0;
    unsigned i=strstr(request.path,"_b.bin")!=0;
    if(request.operation==ABOX_UFS_UPLOAD) {
        files[i].exists=1;files[i].size=request.size;memcpy(files[i].bytes,request.data,request.size);
        *size=request.size;return ABOX_ASYNC_OK;
    }
    if(!files[i].exists) return ABOX_ASYNC_NOT_FOUND;
    *size=files[i].size<request.size?files[i].size:request.size;
    memcpy(request.data,files[i].bytes,*size);
    return *size==request.size?ABOX_ASYNC_OK:ABOX_ASYNC_TRUNCATED;
}
static int file_cancel(void *context,uint64_t op)
{ (void)context;(void)op;leased_request=0;return 1; }
int ABoxEc800UfsAt_Init(ABoxEc800UfsAt *b,ABoxEc800At *at,ABoxEc800Owner owner,const ABoxUfsLeasePort *lease)
{ (void)b;(void)at;(void)owner;(void)lease;leased_request=0;return 1; }
ABoxEc800UfsPort ABoxEc800UfsAt_Port(ABoxEc800UfsAt *b)
{ ABoxEc800UfsPort p={b,file_start,file_poll,file_cancel};return p; }
int OTA_Info_PowerLedgerMarker(void) { return marker; }
int OTA_Info_MarkPowerLedgerInitialized(void) { marker=1;return 1; }
uint8_t OtaService_IsBusy(void) { return ota; }
const char *OtaService_Phase(void) { return provisioning ? "PROVISIONING" : "RETRY_WAIT"; }
uint8_t PatrolEnrollment_TransportBusy(void) { return enrollment_busy; }
int ABoxEc800At_HasPending(const ABoxEc800At *at,ABoxEc800Owner owner)
{ (void)at;assert(owner==ABOX_EC800_OWNER_OTA);return ota_pending; }
uint32_t VehiclePowerOutput_StopGeneration(void) { return generation; }
void VehiclePowerOutput_CancelPendingOn(void) { ++generation; }
uint8_t VehiclePowerOutput_IsOn(void) { return power; }
uint8_t VehiclePowerOutput_Set(uint8_t on) { if(!on)++generation;power=on;return 1; }
void RemoteService_Revoke(void) { ++generation; }
uint8_t RemoteService_HasCachedRequest(const char *id) { return !strcmp(id,"motion-cache"); }
int RemoteService_PowerOnPrepared(uint32_t prior,uint8_t *out)
{ if(prior!=generation||ota){*out=power;return -2;} ++actions;power=1;*out=power;return 1; }
uint8_t RemoteService_HandleControl(const char *p,uint16_t n,char *out,uint16_t cap)
{ (void)p;(void)n;(void)out;(void)cap;++generation;power=0;return 1; }
ABoxClockSnapshot TimeBase_GetSnapshot(void)
{ ABoxClockSnapshot s={0};s.quality=ABOX_CLOCK_SYNCHRONIZED;s.utc_ms=utc;s.mono_ms=mono;s.error_ms=1;return s; }
uint64_t TimeBase_GetMs(void) { return utc; }
uint8_t TimeBase_FormatUtcMs(char *s,uint16_t cap)
{ return (uint8_t)(snprintf(s,cap,"%llu",(unsigned long long)utc)>0); }
uint8_t MQTT_Core_IsReady(void) { return ready; }
uint8_t CloudMessages_ScheduleRawResponse(const char *topic,const char *payload)
{ (void)topic;if(g_cloud.cloud.response_pending)return 0;strcpy(published,payload);g_cloud.cloud.response_pending=1;return 1; }

static const char *topic="/zxwl/abox/device-A/request";
static void poll(unsigned count) { while(count--){++mono;PatrolPowerLedger_Poll();} }
static void make(char *out,const char *id,const char *command,uint64_t expiry)
{ snprintf(out,512,"{\"version\":\"4.0\",\"timestamp\":%llu,\"data\":{\"requestId\":\"%s\",\"command\":\"%s\",\"params\":null,\"expiresAt\":%llu}}",
    (unsigned long long)utc,id,command,(unsigned long long)expiry); }
static void clear_response(void) { g_cloud.cloud.response_pending=0;published[0]='\0'; }
int main(void)
{
    ABoxEc800At at={0};char json[512],out[512];
    strcpy(g_cloud.topic_control_resp,"/zxwl/abox/device-A/response");
    assert(PatrolPowerLedger_Init(&at));
    make(json,"early-stop","power_off",utc+5000);
    power=1;assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003") && !power);
    provisioning=1;poll(20);assert(!files[0].exists && !files[1].exists);provisioning=0;
    ota_pending=1;poll(20);assert(!files[0].exists && !files[1].exists);ota_pending=0;
    enrollment_busy=1;poll(20);assert(!files[0].exists && !files[1].exists);enrollment_busy=0;
    memset(g_cloud.log_snapshot,'x',sizeof(g_cloud.log_snapshot));g_cloud.log_active=1;
    poll(20);assert(!files[0].exists && !files[1].exists && g_cloud.log_snapshot[0]=='x');
    g_cloud.log_active=0;poll(200);assert(marker && PatrolPowerLedger_CheckOther(topic,"query")==200);
    make(json,"first","power_on",utc+5000);
    ota=1;assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003"));assert(!actions);ota=0;
    provisioning=1;assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003"));assert(!actions);provisioning=0;
    ota_pending=1;assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003"));assert(!actions);ota_pending=0;
    enrollment_busy=1;assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003"));assert(!actions);enrollment_busy=0;
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==2);
    assert(PatrolPowerLedger_StorageBusy());poll(200);
    assert(actions==1 && strstr(published,"\"code\":200"));char first[512];strcpy(first,published);clear_response();
    ota=1;assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && !strcmp(first,out) && actions==1);ota=0;
    assert(PatrolPowerLedger_CheckOther(topic,"first")==4006);
    make(json,"first","power_off",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4006") && !power);
    make(json,"motion-cache","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4006"));poll(100);assert(actions==1);
    make(json,"stopped","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==2);VehiclePowerOutput_Set(0);poll(200);
    assert(actions==1 && strstr(published,"superseded by safety stop"));clear_response();
    make(json,"uncertain","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==2);
    while(actions==1) poll(1);
    PatrolPowerLedger_AfterModemReset();poll(200);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003") && actions==2);
    /* A delayed response must not hide replay or request-ID conflicts for
       other protected records; only a new request is held off. */
    g_cloud.cloud.response_pending=1;
    make(json,"held","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==2);poll(200);assert(actions==3);
    make(json,"first","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"\"code\":200"));
    ++utc;make(json,"first","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4006"));
    --utc;
    make(json,"extra","power_on",utc+5000);
    assert(PatrolPowerLedger_Intercept(topic,json,out,sizeof(out))==1 && strstr(out,"4003"));
    clear_response();poll(2);assert(strstr(published,"\"requestId\":\"held\""));clear_response();
    puts("PASS product interceptor, replay, OTA and stop arbitration, shared log buffer, reset");
    return 0;
}
