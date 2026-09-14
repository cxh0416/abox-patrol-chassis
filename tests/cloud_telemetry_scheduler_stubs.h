#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "cloud_telemetry.h"

typedef struct { uint8_t session_active; } ChassisControl;
typedef int AppLogLevel_t;
enum { CLOUD_TX_NONE, CLOUD_TX_HEARTBEAT, CLOUD_TX_STATUS_REPORT,
       CLOUD_TX_RESPONSE, CLOUD_TX_LOG_CHUNK, CLOUD_TX_ALERT };
static uint32_t tick, g_status_report_due, g_heartbeat_due, g_iccid_due;
static uint32_t g_tx_start_sequence;
static int g_tx_kind, g_response_pending, g_log_active, g_alert_pending;
static char g_tx_payload[1100], g_response_payload[1100], g_alert_payload[512];
static const char *g_topic_config_resp, *g_topic_status_report, *g_topic_heartbeat;
static const char *g_response_topic, *g_topic_log, *g_log_request_id;
static CloudTelemetry g_telemetry;
static ChassisControl control;
static uint8_t center_active, bench_active, connected = 1;
static const char* event;
static int last_kind, last_qos, status_packets, heartbeat_packets;
static uint32_t HAL_GetTick(void) { return tick; }
static const ChassisControl* RemoteService_State(void) { return &control; }
static uint8_t RemoteService_BenchPermitted(void) { return bench_active; }
static uint8_t VcuRtos_CenterCalibrationActive(void) { return center_active; }
static unsigned MQTT_Core_GetPubOkSeq(void) { return 0; }
static uint8_t MQTT_Core_IsReady(void) { return connected; }
static uint8_t MQTT_Core_IsPublishActive(void) { return 0; }
static void tx_completed(void) { g_tx_kind = CLOUD_TX_NONE; }
static const char* status_change_reason(uint32_t now) { (void)now; return event; }
static void remember_status_snapshot(uint32_t now) { (void)now; event = 0; }
static uint8_t start_publish(int kind, const char* topic, const char* payload, int qos, int retain)
{
    (void)topic; (void)payload; (void)retain;
    last_kind = kind; last_qos = qos;
    status_packets += kind == CLOUD_TX_STATUS_REPORT;
    heartbeat_packets += kind == CLOUD_TX_HEARTBEAT;
    return 1;
}
static uint8_t build_status_report(char* out, unsigned size, const char* reason)
{ (void)out; (void)size; (void)reason; return 1; }
static uint8_t build_heartbeat(char* out, unsigned size)
{ (void)out; (void)size; return 1; }
static uint8_t build_log_chunk(char* out, unsigned size)
{ (void)out; (void)size; return 1; }
static void schedule_simple_error(const char* topic, const char* id, int code, const char* msg)
{ (void)topic; (void)id; (void)code; (void)msg; }
static uint16_t AppLog_ReadMqttAlert(char* out, unsigned size, AppLogLevel_t* level)
{ (void)out; (void)size; (void)level; return 0; }
static uint8_t build_alert(char* out, unsigned size, const char* line, AppLogLevel_t level)
{ (void)out; (void)size; (void)line; (void)level; return 0; }
static uint8_t MQTT_Core_IsIccidValid(void) { return 1; }
static uint8_t MQTT_Core_IsIccidPending(void) { return 0; }
static void MQTT_Core_RequestIccid(void) {}
