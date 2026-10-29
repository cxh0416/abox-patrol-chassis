#include "cloud_internal.h"
#include <stdio.h>
#include "mqtt_core.h"
#include "time_sync.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "remote_service.h"
#include "center_calibration.h"
#include "patrol_runtime.h"
#include "ota_service.h"

// 清除网络变更前的云发送在途关联。通知公共云服务，重置外部消息类型及候选取消标记，避免旧回执完成新连接的动作。
void CloudTx_NetworkChanged(void)
{
    ABoxCloudService_NetworkChanged(&g_cloud.cloud);
    g_cloud.external_tx_kind = 0U;
    g_cloud.mqtt_was_ready = 0U;
    g_cloud.mqtt_cancel_pending = 0U;
}

// 在被跟踪的 MQTT 发布完成后推进业务状态。响应完成后才缓存结果、允许延后重启、确认回滚或启动候选切换；心跳证明和日志分片也在此推进。
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

// 查询云发送端口是否可提交发布。直接复用 MQTT 核心就绪状态，未就绪时保留调度任务等待后续轮询。
static int tx_ready(void *context) { (void)context; return MQTT_Core_IsReady(); }
// 查询 MQTT 是否已有发布事务占用发送路径。用于保证共享载荷和回执关联不被并发覆盖。
static int tx_active(void *context) { (void)context; return MQTT_Core_IsPublishActive(); }
// 查询指定发布操作的最终或在途回执状态。使用 operation 关联准确事务，不用其他消息的成功序号代替本次结果。
static ABoxMqttReceiptStatus tx_receipt(void *context, uint64_t operation)
{ (void)context; return MQTT_Core_PublishResult(operation); }
// 在云发布失败后安排传输恢复。仅当 MQTT 仍处于就绪状态时请求重连，避免重复打断已有恢复流程。
static void tx_failed(void *context)
{ (void)context; if (MQTT_Core_IsReady()) MQTT_Core_RequestReconnect(); }
// 编码并提交公共云调度器选定的消息。按任务类型设置 Topic、QoS 和 retain，生成可跟踪 operation；返回提交结果而非云端业务处理完成。
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
// 提交日志或告警等外部类型发布并登记归属。成功返回 1，提交失败返回 -1；仅在核心接受发布后设置 external_tx_kind。
static int send_external(CloudTxKind_t kind, const char *topic, const char *payload,
                         uint8_t qos, uint64_t *operation)
{
    if (!MQTT_Core_PublishExTracked(topic, payload, qos, 0U, operation)) return -1;
    g_cloud.external_tx_kind=(uint8_t)kind;
    return 1;
}
// 在主业务之外调度时间同步、日志与告警。不覆盖已有在途发布，按现有优先级选择一条外部消息；返回值区分无任务、成功提交和失败。
static int tx_external(void *context, uint64_t *operation)
{
    (void)context;
    TimeSync_Task();
    if (MQTT_Core_IsPublishActive()) return 0;
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
// 执行一次云消息与状态事件发送调度。比较状态变化、轮询公共发送器，空闲时再安排 ICCID 查询；消息完成以跟踪回执为准。
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
