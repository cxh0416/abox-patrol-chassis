#include "cloud_internal.h"
#include <stdio.h>
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "remote_service.h"
#include "center_calibration.h"
#include "patrol_runtime.h"
#include "ota_service.h"

void CloudTx_NetworkChanged(void)
{
    ABoxCloudService_NetworkChanged(&g_cloud.cloud);
    g_cloud.external_tx_kind = 0U;
    g_cloud.mqtt_was_ready = 0U;
    g_cloud.mqtt_cancel_pending = 0U;
}

static void tx_completed(void *context, ABoxCloudJobKind kind)
{
    char result[128];
    CloudTxKind_t completed = kind == ABOX_CLOUD_JOB_EXTERNAL
        ? (CloudTxKind_t)g_cloud.external_tx_kind : (CloudTxKind_t)kind;
    (void)context;
    g_cloud.external_tx_kind = 0U;
    if (completed == CLOUD_TX_RESPONSE)
    {
        CloudMessages_CacheResponse(g_cloud.cloud.response_request_id, g_cloud.response_topic, g_cloud.response_payload);
        g_cloud.cloud.response_pending = 0U;
        if (g_cloud.response_reboots)
        {
            g_cloud.reboot_pending = 1U;
            g_cloud.reboot_due = CloudPort_Now() + CLOUD_REBOOT_DELAY_MS;
        }
        g_cloud.response_reboots = 0U;
        if (g_cloud.response_ack_rollback)
        {
            OtaService_AcknowledgeRollback();
            g_cloud.response_ack_rollback = 0U;
        }
        if (g_cloud.mqtt_candidate_pending)
        {
            g_cloud.mqtt_candidate_pending = 0U;
            MqttTrialManager_Accepted(CloudPort_Now());
        }
        if (g_cloud.mqtt_cancel_pending)
        {
            g_cloud.mqtt_cancel_pending = 0U;
            MqttTrialManager_Cancel(CloudPort_Now());
        }
    }
    else if (completed == CLOUD_TX_HEARTBEAT)
    {
        MqttTrialManager_OnHeartbeatProof(MQTT_Core_GetPubOkSeq(), CloudPort_Now());
    }
    else if (completed == CLOUD_TX_LOG_CHUNK)
    {
        g_cloud.log_chunk_index++;
        if (g_cloud.log_chunk_index >= g_cloud.log_chunk_total)
        {
            g_cloud.log_active = 0U;
            (void)snprintf(result, sizeof(result), "{\"lineCount\":%u,\"dropped\":%lu}",
                           (unsigned int)g_cloud.log_line_count, (unsigned long)AppLog_GetMqttDroppedCount());
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, g_cloud.log_request_id, 200U, "OK", result, 0U);
        }
    }
    else if (completed == CLOUD_TX_ALERT) g_cloud.alert_pending = 0U;
}

static int tx_ready(void *context) { (void)context; return MQTT_Core_IsReady(); }
static int tx_active(void *context) { (void)context; return MQTT_Core_IsPublishActive(); }
static ABoxMqttReceiptStatus tx_receipt(void *context, uint64_t operation)
{ (void)context; return MQTT_Core_PublishResult(operation); }
static void tx_failed(void *context)
{ (void)context; if (MQTT_Core_IsReady()) MQTT_Core_RequestReconnect(); }
static int tx_publish(void *context, const ABoxCloudJob *job, uint64_t *operation)
{
    const char *topic = 0, *payload = g_cloud.tx_payload;
    uint8_t encoded = 0U, qos = 1U, retain = 0U;
    (void)context;
    switch (job->kind) {
    case ABOX_CLOUD_JOB_RESPONSE:
        if (!g_cloud.cloud.response_pending) {
            (void)CloudMessages_ScheduleSyncResponse(job->request_id);
            return 0;
        }
        topic=g_cloud.response_topic; payload=g_cloud.response_payload; encoded=1U; break;
    case ABOX_CLOUD_JOB_MANIFEST:
        topic=g_cloud.topic_manifest; retain=1U;
        encoded=CloudMessages_BuildManifest(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)); break;
    case ABOX_CLOUD_JOB_HEARTBEAT:
        topic=g_cloud.topic_heartbeat; qos=0U;
        encoded=CloudMessages_BuildHeartbeat(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)); break;
    case ABOX_CLOUD_JOB_STATE:
        topic=g_cloud.topic_status_report;
        g_cloud.status_report_request_id=job->request_id;
        encoded=CloudMessages_BuildStatusReport(g_cloud.tx_payload, sizeof(g_cloud.tx_payload), job->reason);
        g_cloud.status_report_request_id=0; break;
    default: return 0;
    }
    if (!encoded || !MQTT_Core_PublishExTracked(topic, payload, qos, retain, operation)) return 0;
    if (job->kind == ABOX_CLOUD_JOB_STATE) CloudMessages_RememberStatusSnapshot(CloudPort_Now());
    return 1;
}
static int send_external(CloudTxKind_t kind, const char *topic, const char *payload,
                         uint8_t qos, uint64_t *operation)
{
    if (!MQTT_Core_PublishExTracked(topic, payload, qos, 0U, operation)) return -1;
    g_cloud.external_tx_kind=(uint8_t)kind;
    return 1;
}
static int tx_external(void *context, uint64_t *operation)
{
    (void)context;
    if (g_cloud.log_active)
    {
        if (!CloudMessages_BuildLogChunk(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)))
        {
            g_cloud.log_active = 0U;
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, g_cloud.log_request_id, 500U, "LOG_ENCODING_FAILED");
            return -1;
        }
        return send_external(CLOUD_TX_LOG_CHUNK, g_cloud.topic_log, g_cloud.tx_payload, 1U, operation);
    }
    if (g_cloud.alert_pending)
    {
        return send_external(CLOUD_TX_ALERT, g_cloud.topic_log, g_cloud.alert_payload, 0U, operation);
    }
    {
        char line[160];
        AppLogLevel_t level;
        if (AppLog_ReadMqttAlert(line, sizeof(line), &level) > 0U && CloudMessages_BuildAlert(g_cloud.alert_payload, sizeof(g_cloud.alert_payload), line, level))
        {
            g_cloud.alert_pending = 1U;
            return send_external(CLOUD_TX_ALERT, g_cloud.topic_log, g_cloud.alert_payload, 0U, operation);
        }
    }
    return 0;
}
static const ABoxCloudServicePort tx_port = {
    .ready=tx_ready, .publish_active=tx_active, .receipt=tx_receipt,
    .publish=tx_publish, .external=tx_external, .completed=tx_completed, .failed=tx_failed
};
void CloudTx_Service(void)
{
    uint32_t now=CloudPort_Now();
    const ChassisControl *control=RemoteService_State();
    if (CloudTelemetry_Update(&g_cloud.telemetry, now,
            control->session_active || CenterCalibration_IsActive() || PatrolRuntime_AutoCenterActive())) {
        g_cloud.cloud.status_due=now;
        g_cloud.cloud.heartbeat_due=now;
    }
    g_cloud.cloud.options.heartbeat_period_ms=g_cloud.telemetry.fast
        ? CLOUD_ACTIVE_HEARTBEAT_MS : CLOUD_IDLE_HEARTBEAT_MS;
    g_cloud.cloud.options.status_period_ms=g_cloud.telemetry.fast
        ? CLOUD_ACTIVE_STATUS_MS : CLOUD_IDLE_STATUS_MS;
    if (!ABoxCloudService_Bootstrapping(&g_cloud.cloud) && MQTT_Core_IsReady()) {
        const char *reason=CloudMessages_StatusChangeReason(now);
        if (reason) {
            CloudTelemetry_Wake(&g_cloud.telemetry, now);
            (void)ABoxCloudService_WakeState(&g_cloud.cloud, reason);
        }
    }
    if (ABoxCloudService_Poll(&g_cloud.cloud, &tx_port, now) != ABOX_CLOUD_POLL_IDLE) return;
    if (!MQTT_Core_IsIccidValid() && !MQTT_Core_IsIccidPending() && (int32_t)(now - g_cloud.iccid_due) >= 0)
    {
        MQTT_Core_RequestIccid();
        g_cloud.iccid_due = now + 30000U;
    }
}
