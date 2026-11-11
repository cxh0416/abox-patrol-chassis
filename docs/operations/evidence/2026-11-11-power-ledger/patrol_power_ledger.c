#include "patrol_power_ledger.h"
#include "patrol_power_ledger_fsm.h"
#include "abox_ec800_ufs_at.h"
#include "abox_mqtt_v4.h"
#include "abox_mqtt_request.h"
#include "chassis_cloud_protocol.h"
#include "cloud_internal.h"
#include "mqtt_core.h"
#include "ota_info.h"
#include "ota_service.h"
#include "remote_service.h"
#include "time_base.h"
#include "vehicle_power_output.h"
#include <stdio.h>
#include <string.h>

#define POWER_LEDGER_OWNER (ABOX_EC800_OWNER_PRODUCT_BASE + 6U)
_Static_assert(CLOUD_LOG_SNAPSHOT_SIZE >= PATROL_POWER_WORKSPACE_SIZE,
               "log workspace must fit the complete power journal image");
static ABoxEc800UfsAt backend;
static PatrolPowerLedgerFsm ledger;
static ABoxEc800At *at_core;
static uint32_t stop_generation;
static uint8_t leased, initialized, reset_pending, delivery_pending, delivery_slot;

static int marker_read(void *context) { (void)context; return OTA_Info_PowerLedgerMarker(); }
static int marker_write(void *context) { (void)context; return OTA_Info_MarkPowerLedgerInitialized(); }
static int acquire(void *context)
{
    (void)context;
    if (leased || g_cloud.log_active || OtaService_IsBusy() || at_core->quarantined) return 0;
    leased=1U; return 1;
}
static void release(void *context) { (void)context; leased=0U; }
static int admit(void *context) { (void)context; return !OtaService_IsBusy(); }
static int act_on(void *context, uint8_t *power)
{ (void)context; return RemoteService_PowerOnPrepared(stop_generation,power); }
static void safe_stop(void *context)
{ (void)context; RemoteService_Revoke(); (void)VehiclePowerOutput_Set(0U); }
static uint8_t power_state(void *context) { (void)context; return VehiclePowerOutput_IsOn(); }
static uint64_t utc_now(void *context) { (void)context; return TimeBase_GetMs(); }

static int init_state(void)
{
    ABoxEc800UfsPort files;
    const PatrolPowerLedgerPort port={0,marker_read,marker_write,acquire,release,admit,act_on,safe_stop,power_state,utc_now};
    if (!ABoxEc800UfsAt_Init(&backend,at_core,POWER_LEDGER_OWNER,0)) return 0;
    files=ABoxEc800UfsAt_Port(&backend);
    return PatrolPowerLedgerFsm_Init(&ledger,&files,&port,
        "UFS:patrol_adapter_lab_a.bin","UFS:patrol_adapter_lab_b.bin",
        (uint8_t *)g_cloud.log_snapshot,PATROL_POWER_WORKSPACE_SIZE);
}
int PatrolPowerLedger_Init(ABoxEc800At *at)
{
    at_core=at; leased=reset_pending=delivery_pending=0U;
    initialized=(uint8_t)(at && init_state());
    return initialized;
}
void PatrolPowerLedger_AfterModemReset(void)
{
    VehiclePowerOutput_CancelPendingOn();
    delivery_pending=0U; reset_pending=1U;
}
void PatrolPowerLedger_NetworkChanged(void)
{ VehiclePowerOutput_CancelPendingOn(); delivery_pending=0U; }
int PatrolPowerLedger_StorageBusy(void)
{ return initialized && (leased || PatrolPowerLedgerFsm_Busy(&ledger)); }

static int identity(const char *topic,char out[32])
{
    const char *end;size_t n;
    if (!topic || strncmp(topic,"/zxwl/abox/",11U)) return 0;
    topic+=11U;end=strchr(topic,'/');
    if (!end || strcmp(end,"/request")) return 0;
    n=(size_t)(end-topic);if (!n || n>=32U) return 0;
    memcpy(out,topic,n);out[n]='\0';return ABoxMqttV4_DeviceIdValid(out);
}
static int error_response(const char *id,uint16_t code,const char *msg,char *out,uint16_t cap)
{
    char stamp[21];int n;
    if (!TimeBase_FormatUtcMs(stamp,sizeof(stamp))) strcpy(stamp,"0");
    n=snprintf(out,cap,"{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\"}}",
        stamp,id,(unsigned)code,msg);
    return n>0 && n<cap;
}
uint16_t PatrolPowerLedger_CheckOther(const char *topic,const char *id)
{
    char device[32];size_t slot;ABoxClockSnapshot clock;int state;
    if (!identity(topic,device) || !ABoxMqttV4_IdValid(id,ABOX_MQTT_ID_REQUEST)) return 4001U;
    if (!initialized || reset_pending) return 4003U;
    clock=TimeBase_GetSnapshot();
    state=PatrolPowerLedgerFsm_Request(&ledger,device,id,0,0,&clock,&slot);
    return state==PATROL_POWER_CONFLICT ? 4006U : state==PATROL_POWER_FAILED ? 4003U : 200U;
}
int PatrolPowerLedger_Intercept(const char *topic,const char *payload,char *out,uint16_t cap)
{
    ChassisCloudCommand cmd;ABoxClockSnapshot clock;char device[32];size_t slot=0;
    int state,is_on,is_stop;
    uint32_t received_generation=VehiclePowerOutput_StopGeneration();
    if (!identity(topic,device) || !payload || !out || !cap ||
        ChassisCloud_Parse(payload,(uint16_t)strlen(payload),&cmd)!=CHASSIS_CLOUD_OK ||
        !ABoxMqttV4_IdValid(cmd.request_id,ABOX_MQTT_ID_REQUEST)) return 0;
    is_on=cmd.type==CHASSIS_CLOUD_COMMAND_POWER_ON;
    is_stop=cmd.type==CHASSIS_CLOUD_COMMAND_POWER_OFF ||
        (cmd.type==CHASSIS_CLOUD_COMMAND_MOTION && !strcmp(cmd.action,"stop"));
    if (!initialized || reset_pending) {
        if (is_stop) return 0;
        return error_response(cmd.request_id,4003U,"power ledger unavailable",out,cap) ? 1 : 2;
    }
    if (is_on && delivery_pending && ledger.active==PATROL_POWER_SLOTS &&
        (strcmp(ledger.entry[delivery_slot].id,cmd.request_id) ||
         strcmp(ledger.entry[delivery_slot].device,device)))
        return error_response(cmd.request_id,4003U,"response pending",out,cap) ? 1 : 2;
    if (is_on && RemoteService_HasCachedRequest(cmd.request_id))
        return error_response(cmd.request_id,4006U,"requestId conflict",out,cap) ? 1 : 2;
    clock=TimeBase_GetSnapshot();
    state=PatrolPowerLedgerFsm_Request(&ledger,device,cmd.request_id,is_on,cmd.expires_at,&clock,&slot);
    if (!is_on) {
        if (state==PATROL_POWER_FAILED && !is_stop)
            return error_response(cmd.request_id,4003U,"power ledger unavailable",out,cap) ? 1 : 2;
        if (state!=PATROL_POWER_CONFLICT) return 0;
        if (is_stop) (void)RemoteService_HandleControl(payload,(uint16_t)strlen(payload),out,cap);
        return error_response(cmd.request_id,4006U,"requestId conflict",out,cap) ? 1 : 2;
    }
    switch(state) {
    case PATROL_POWER_NEW:
        stop_generation=received_generation;
        delivery_slot=(uint8_t)slot;delivery_pending=1U;return 2;
    case PATROL_POWER_WAIT:
        delivery_slot=(uint8_t)slot;delivery_pending=1U;return 2;
    case PATROL_POWER_REPLAY:
        return PatrolPowerLedgerFsm_Response(&ledger,slot,out,cap)>0 ? 1 : 2;
    case PATROL_POWER_CONFLICT:
        return error_response(cmd.request_id,4006U,"requestId conflict",out,cap) ? 1 : 2;
    case PATROL_POWER_EXPIRED:
        return error_response(cmd.request_id,4005U,"request expired",out,cap) ? 1 : 2;
    default:
        return error_response(cmd.request_id,4003U,"power ledger unavailable or busy",out,cap) ? 1 : 2;
    }
}
void PatrolPowerLedger_Poll(void)
{
    ABoxClockSnapshot clock=TimeBase_GetSnapshot();
    if (!initialized) return;
    if (reset_pending) {
        reset_pending=0U;
        ABoxUfsJournal_OnModemReset(&ledger.journal);
        leased=0U;
        if (!init_state()) { initialized=0U; return; }
    }
    if (!ledger.ready && ABoxUfsJournal_Status(&ledger.journal)==0)
        (void)PatrolPowerLedgerFsm_Load(&ledger,clock.mono_ms);
    PatrolPowerLedgerFsm_Poll(&ledger,&clock);
    if (ledger.failure_pending && (!delivery_pending || (!g_cloud.cloud.response_pending && MQTT_Core_IsReady()))) {
        char device[32],id[65];uint16_t code;
        if (PatrolPowerLedgerFsm_TakeFailure(&ledger,device,sizeof(device),id,sizeof(id),&code)) {
            if (delivery_pending && error_response(id,code,"power outcome unavailable",g_cloud.tx_payload,sizeof(g_cloud.tx_payload)))
                (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp,g_cloud.tx_payload);
            delivery_pending=0U;
        }
    }
    if (delivery_pending && ledger.ready && ledger.active==PATROL_POWER_SLOTS &&
        !g_cloud.cloud.response_pending && MQTT_Core_IsReady() &&
        PatrolPowerLedgerFsm_Response(&ledger,delivery_slot,g_cloud.tx_payload,sizeof(g_cloud.tx_payload))>0) {
        if (CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp,g_cloud.tx_payload)) delivery_pending=0U;
    }
}
