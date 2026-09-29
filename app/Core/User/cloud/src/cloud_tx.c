#include "cloud_internal.h"
#include <stdio.h>
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "remote_service.h"
#include "center_calibration.h"
#include "ota_service.h"

static uint8_t start_publish(CloudTxKind_t kind, const char* topic, const char* payload,
                             uint8_t qos, uint8_t retain)
{
    if ((g_cloud.tx_kind != CLOUD_TX_NONE) || !MQTT_Core_IsReady() || MQTT_Core_IsPublishActive()) return 0U;
    if (!MQTT_Core_PublishExTracked(topic, payload, qos, retain,
                                    &g_cloud.tx_operation)) return 0U;
    g_cloud.tx_kind = kind;
    return 1U;
}
void CloudTx_NetworkChanged(void)
{
    ABoxCloudBootstrap_NetworkChanged(&g_cloud.cloud_bootstrap);
    g_cloud.cloud_publish_generation = 0U;
    g_cloud.tx_kind = CLOUD_TX_NONE;
    g_cloud.tx_operation = 0U;
    g_cloud.mqtt_was_ready = 0U;
    g_cloud.mqtt_cancel_pending = 0U;
}

static void tx_completed(void)
{
    char result[128];
    CloudTxKind_t completed = g_cloud.tx_kind;
    if (g_cloud.cloud_publish_generation != 0U) {
        (void)ABoxCloudBootstrap_Receipt(&g_cloud.cloud_bootstrap,
                                         g_cloud.cloud_publish_generation,
                                         g_cloud.tx_operation, 1);
        g_cloud.cloud_publish_generation = 0U;
    }
    g_cloud.tx_kind = CLOUD_TX_NONE;
    if (completed == CLOUD_TX_RESPONSE)
    {
        CloudMessages_CacheResponse(g_cloud.response_request_id, g_cloud.response_topic, g_cloud.response_payload);
        g_cloud.response_pending = 0U;
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
    else if (completed == CLOUD_TX_STATUS_REPORT)
    {
        g_cloud.status_report_request_id[0] = '\0';
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

void CloudTx_Service(void)
{
    uint32_t now = CloudPort_Now();
    const ChassisControl* control = RemoteService_State();
    if (CloudTelemetry_Update(&g_cloud.telemetry, now,
            control->session_active || CenterCalibration_IsActive() ||
            CenterCalibration_IsActive())) {
        g_cloud.status_report_due = now;
        g_cloud.heartbeat_due = now;
    }
    if (g_cloud.tx_kind != CLOUD_TX_NONE)
    {
        ABoxMqttReceiptStatus result = MQTT_Core_PublishResult(g_cloud.tx_operation);
        if (result == ABOX_MQTT_RECEIPT_CONFIRMED) tx_completed();
        else if (result != ABOX_MQTT_RECEIPT_PENDING || !MQTT_Core_IsPublishActive()) {
            if (g_cloud.cloud_publish_generation != 0U) {
                (void)ABoxCloudBootstrap_Receipt(&g_cloud.cloud_bootstrap,
                                                 g_cloud.cloud_publish_generation,
                                                 g_cloud.tx_operation, 0);
                g_cloud.cloud_publish_generation = 0U;
            }
            /* A failed modem publish has no completion sequence. Recover the
             * link instead of leaving telemetry permanently queued. */
            g_cloud.tx_kind = CLOUD_TX_NONE;
            if (MQTT_Core_IsReady()) MQTT_Core_RequestReconnect();
            return;
        }
        else return;
    }
    if (!MQTT_Core_IsReady() || MQTT_Core_IsPublishActive()) return;

    /* V4 reconnect order is Manifest -> Heartbeat -> complete State. */
    if (ABoxCloudBootstrap_Active(&g_cloud.cloud_bootstrap))
    {
        ABoxCloudJob job;
        uint8_t published = 0U;
        if (!ABoxCloudBootstrap_Next(&g_cloud.cloud_bootstrap, &job)) return;
        if (job.kind == ABOX_CLOUD_JOB_MANIFEST &&
            CloudMessages_BuildManifest(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)))
            published = start_publish(CLOUD_TX_MANIFEST, g_cloud.topic_manifest,
                                      g_cloud.tx_payload, 1U, 1U);
        else if (job.kind == ABOX_CLOUD_JOB_HEARTBEAT &&
                 CloudMessages_BuildHeartbeat(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)))
            published = start_publish(CLOUD_TX_HEARTBEAT, g_cloud.topic_heartbeat,
                                      g_cloud.tx_payload, 0U, 0U);
        else if (job.kind == ABOX_CLOUD_JOB_STATE &&
                 CloudMessages_BuildStatusReport(g_cloud.tx_payload,
                                                 sizeof(g_cloud.tx_payload), "reconnect")) {
            published = start_publish(CLOUD_TX_STATUS_REPORT,
                                      g_cloud.topic_status_report, g_cloud.tx_payload, 1U, 0U);
            if (published) CloudMessages_RememberStatusSnapshot(now);
        }
        if (published) {
            g_cloud.cloud_publish_generation = job.generation;
            if (!ABoxCloudBootstrap_Begin(&g_cloud.cloud_bootstrap,
                                          &job, g_cloud.tx_operation))
                MQTT_Core_RequestReconnect();
        }
        return;
    }

    if (g_cloud.response_pending)
    {
        (void)start_publish(CLOUD_TX_RESPONSE, g_cloud.response_topic, g_cloud.response_payload, 1U, 0U);
        return;
    }
    if (g_cloud.sync_state_request_id[0] != '\0' && g_cloud.status_report_request_id[0] == '\0')
    {
        if (CloudMessages_ScheduleSyncResponse(g_cloud.sync_state_request_id)) {
            CloudMessages_CopyString(g_cloud.status_report_request_id,
                                     sizeof(g_cloud.status_report_request_id), g_cloud.sync_state_request_id);
            g_cloud.sync_state_request_id[0] = '\0';
            g_cloud.status_report_due = now;
        }
        return;
    }
    {
        const char* event_reason = CloudMessages_StatusChangeReason(now);
        if (event_reason != 0)
        {
            /* Keep a short observation tail after faults and power/state changes. */
            CloudTelemetry_Wake(&g_cloud.telemetry, now);
            if (CloudMessages_BuildStatusReport(g_cloud.tx_payload, sizeof(g_cloud.tx_payload), event_reason) &&
                start_publish(CLOUD_TX_STATUS_REPORT, g_cloud.topic_status_report, g_cloud.tx_payload, 1U, 0U))
                CloudMessages_RememberStatusSnapshot(now);
            return;
        }
    }
    if ((int32_t)(now - g_cloud.heartbeat_due) >= 0)
    {
        if (CloudMessages_BuildHeartbeat(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)) &&
            start_publish(CLOUD_TX_HEARTBEAT, g_cloud.topic_heartbeat, g_cloud.tx_payload, 0U, 0U))
            g_cloud.heartbeat_due = now + (g_cloud.telemetry.fast ? CLOUD_ACTIVE_HEARTBEAT_MS : CLOUD_IDLE_HEARTBEAT_MS);
        return;
    }
    if ((int32_t)(now - g_cloud.status_report_due) >= 0)
    {
        if (CloudMessages_BuildStatusReport(g_cloud.tx_payload, sizeof(g_cloud.tx_payload), "periodic") &&
            start_publish(CLOUD_TX_STATUS_REPORT, g_cloud.topic_status_report, g_cloud.tx_payload, 1U, 0U))
        {
            g_cloud.status_report_due = now + (g_cloud.telemetry.fast ? CLOUD_ACTIVE_STATUS_MS : CLOUD_IDLE_STATUS_MS);
            CloudMessages_RememberStatusSnapshot(now);
        }
        return;
    }
    if (g_cloud.log_active)
    {
        if (!CloudMessages_BuildLogChunk(g_cloud.tx_payload, sizeof(g_cloud.tx_payload)))
        {
            g_cloud.log_active = 0U;
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, g_cloud.log_request_id, 500U, "LOG_ENCODING_FAILED");
            return;
        }
        (void)start_publish(CLOUD_TX_LOG_CHUNK, g_cloud.topic_log, g_cloud.tx_payload, 1U, 0U);
        return;
    }
    if (g_cloud.alert_pending)
    {
        (void)start_publish(CLOUD_TX_ALERT, g_cloud.topic_log, g_cloud.alert_payload, 0U, 0U);
        return;
    }
    {
        char line[160];
        AppLogLevel_t level;
        if (AppLog_ReadMqttAlert(line, sizeof(line), &level) > 0U && CloudMessages_BuildAlert(g_cloud.alert_payload, sizeof(g_cloud.alert_payload), line, level))
        {
            g_cloud.alert_pending = 1U;
            (void)start_publish(CLOUD_TX_ALERT, g_cloud.topic_log, g_cloud.alert_payload, 0U, 0U);
            return;
        }
    }
    if (!MQTT_Core_IsIccidValid() && !MQTT_Core_IsIccidPending() && (int32_t)(now - g_cloud.iccid_due) >= 0)
    {
        MQTT_Core_RequestIccid();
        g_cloud.iccid_due = now + 30000U;
    }
}
