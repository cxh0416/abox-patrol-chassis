#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "cloud_internal.h"
#include "remote_service.h"
#include "mqtt_core.h"
#include "ota_service.h"

CloudContext g_cloud;
static uint32_t tick, pub_sequence;
static ChassisControl control;
static uint8_t center_active, bench_active, connected = 1, publish_active;
static const char* event;
static int last_kind, last_qos, status_packets, heartbeat_packets, cached_responses;

uint32_t CloudPort_Now(void) { return tick; }
const ChassisControl* RemoteService_State(void) { return &control; }
uint8_t RemoteService_BenchPermitted(void) { return bench_active; }
uint8_t CenterCalibration_IsActive(void) { return center_active; }
uint32_t MQTT_Core_GetPubOkSeq(void) { return pub_sequence; }
void MqttTrialManager_OnHeartbeatProof(uint32_t publish_sequence, uint32_t now)
{ (void)publish_sequence; (void)now; }
uint8_t MQTT_Core_IsReady(void) { return connected; }
uint8_t MQTT_Core_IsPublishActive(void) { return publish_active; }
uint8_t MQTT_Core_PublishEx(const char* topic, const char* payload, uint8_t qos, uint8_t retain)
{
    (void)payload; (void)retain;
    last_kind = topic == g_cloud.topic_status_report ? CLOUD_TX_STATUS_REPORT :
        topic == g_cloud.topic_heartbeat ? CLOUD_TX_HEARTBEAT : CLOUD_TX_RESPONSE;
    last_qos = qos;
    status_packets += last_kind == CLOUD_TX_STATUS_REPORT;
    heartbeat_packets += last_kind == CLOUD_TX_HEARTBEAT;
    publish_active = 1;
    return 1;
}
const char* CloudMessages_StatusChangeReason(uint32_t now) { (void)now; return event; }
void CloudMessages_RememberStatusSnapshot(uint32_t now) { (void)now; event = 0; }
uint8_t CloudMessages_BuildStatusReport(char* out, uint16_t size, const char* reason)
{ (void)out; (void)size; (void)reason; return 1; }
uint8_t CloudMessages_BuildHeartbeat(char* out, uint16_t size)
{ (void)out; (void)size; return 1; }
uint8_t CloudMessages_BuildManifest(char* out, uint16_t size)
{ (void)out; (void)size; return 1; }
uint8_t CloudMessages_BuildLogChunk(char* out, uint16_t size)
{ (void)out; (void)size; return 1; }
void CloudMessages_ScheduleSimpleError(const char* topic, const char* id, uint16_t code, const char* msg)
{ (void)topic; (void)id; (void)code; (void)msg; }
uint16_t AppLog_ReadMqttAlert(char* out, uint16_t size, AppLogLevel_t* level)
{ (void)out; (void)size; (void)level; return 0; }
uint8_t CloudMessages_BuildAlert(char* out, uint16_t size, const char* line, AppLogLevel_t level)
{ (void)out; (void)size; (void)line; (void)level; return 0; }
uint8_t MQTT_Core_IsIccidValid(void) { return 1; }
uint8_t MQTT_Core_IsIccidPending(void) { return 0; }
void MQTT_Core_RequestIccid(void) {}
void OtaService_AcknowledgeRollback(void) {}
uint32_t AppLog_GetMqttDroppedCount(void) { return 0; }
void CloudMessages_CacheResponse(const char* id, const char* topic, const char* payload)
{ (void)id; (void)topic; (void)payload; cached_responses++; }
uint8_t CloudMessages_ScheduleResponse(const char* topic, const char* id, uint16_t code,
                               const char* msg, const char* result, uint8_t reboot)
{ (void)topic; (void)id; (void)code; (void)msg; (void)result; (void)reboot; return 1; }

static void complete_publish(void)
{
    if (publish_active) {
        publish_active = 0;
        pub_sequence++;
    }
}
