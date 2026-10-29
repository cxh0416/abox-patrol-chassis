#include "abox_cloud_dispatch.h"
#include "cloud_internal.h"
#include "cJSON.h"
#include "time_base.h"
#include "time_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_version.h"
#include "boot_state.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "mqtt_trial_persistent.h"
#include "ota_service.h"
#include "remote_service.h"
#include "patrol_config.h"
#include "patrol_runtime.h"
#include "center_calibration.h"
#include "patrol_can_status.h"
#include "vehicle_power_output.h"

// 检查当前 Boot State 是否允许管理请求重启。避免在安装、试运行或回滚关键阶段插入不兼容的复位操作，状态读取失败时按代码的拒绝路径处理。
static uint8_t boot_state_allows_reboot(void)
{
    BootStateRecord_t state;
    if (!BootState_Load(&state)) return 1U;
    return (state.state == BOOT_STATE_NORMAL || state.state == BOOT_STATE_CONFIRMED) ? 1U : 0U;
}
// 把试连证明文本转换为内部 64 位标识。供 MQTT 试连会话关联使用，不把该数值当成运动授权或硬件验收结果。
static uint64_t mqtt_trial_proof_id(const char *text)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    while (text != 0 && *text != '\0') { hash ^= (uint8_t)*text++; hash *= UINT64_C(1099511628211); }
    return hash != 0U ? hash : 1U;
}
// 把 64 位 MQTT 试连会话号格式化为十进制文本。output 必须能容纳最多 20 位数字与终止符，供协议响应关联会话。
static void mqtt_trial_session_decimal(uint64_t value, char output[21])
{
    char reverse[20];
    unsigned count = 0U, i;
    do { reverse[count++] = (char)('0' + (value % 10U)); value /= 10U; } while (value != 0U);
    for (i = 0U; i < count; ++i) output[i] = reverse[count - i - 1U];
    output[count] = '\0';
}

// 执行已经解析的内部维护与配置请求。按动作检查 OTA、标定、供电及持久化条件，调度响应和必要的延后重启；涉及 MQTT 试连时先响应再切换连接。
static __attribute__((noinline)) void handle_config_request(const CloudProtocolRequest_t* request)
{
    /* The Remote task serializes command handling and TX. A response is copied
     * into response_payload before TX reuses this buffer. */
    char* result = g_cloud.tx_payload;
    const uint16_t result_size = sizeof(g_cloud.tx_payload);
    CloudProtocolAction_t action;
    const char* request_id;
    if (request == 0) return;
    action = request->action;
    request_id = request->request_id;
    if (g_cloud.mqtt_persistent_active && action != CLOUD_PROTOCOL_ACTION_GET_INFO &&
        action != CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_GET) {
        CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "MQTT_TRIAL_BUSY");
        return;
    }
    /* A maintenance query opens a bounded observation window, never a motion permit. */
    CloudTelemetry_Wake(&g_cloud.telemetry, CloudPort_Now());
    if (action == CLOUD_PROTOCOL_ACTION_GET_INFO)
    {
        CloudMessages_BuildInfoResult(result, result_size);
        if (CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U))
            g_cloud.response_ack_rollback = 1U;
        else
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_CAN_DIAG)
    {
        PatrolDiagnosticsSnapshot snapshot;
        PatrolRuntime_GetDiagnostics(&snapshot);
        if (!PatrolDiagnostics_FormatCanJson(&snapshot, PatrolRtos_CanBusOff(),
                PatrolRtos_CanBusOffActive(), PatrolRtos_CanRxOverflowCount(),
                PatrolRtos_CanRawRxCount(0U), PatrolRtos_CanRawRxCount(1U), result, result_size))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "DIAGNOSTIC_TOO_LARGE");
        else if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_CALIBRATION)
    {
        CloudMessages_BuildCalibrationResult(result, result_size);
        if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_DIAGNOSTICS)
    {
        if (!CenterCalibration_FormatDiagnostics(result, result_size))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "DIAGNOSTIC_TOO_LARGE");
        else if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_START ||
             action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_GET ||
             action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_JOG ||
             action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_COMMIT ||
             action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_CANCEL)
    {
        uint8_t accepted = 1U;
        uint16_t code = 200U;
        const char* message = "OK";
        if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_START)
            accepted = CenterCalibration_Start();
        else if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_GET)
            accepted = CenterCalibration_KeepAlive(request->center_session_id);
        else if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_JOG)
            accepted = CenterCalibration_Jog(request->center_session_id,
                                                     request->center_delta_mdeg);
        else if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_COMMIT) {
            accepted = CenterCalibration_Commit(request->center_session_id);
            code = 202U;
            message = "CENTER_CALIBRATION_COMMIT_STARTED";
        } else if (action == CLOUD_PROTOCOL_ACTION_CENTER_CAL_CANCEL)
            accepted = CenterCalibration_Cancel(request->center_session_id);
        if (!accepted) {
            if (!CenterCalibration_Format(result, result_size) ||
                !CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 409U,
                                   "CENTER_CALIBRATION_NOT_READY", result, 0U))
                CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U,
                                      "CENTER_CALIBRATION_NOT_READY");
        } else {
            if (!CenterCalibration_Format(result, result_size) ||
                !CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, code, message, result, 0U))
                CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U,
                                      "CENTER_DIAGNOSTIC_TOO_LARGE");
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_RESET_MOTION_FAULT) {
        if (!PatrolRuntime_ResetFault())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "POWER_OFF_AND_OTA_IDLE_REQUIRED");
        else (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U,
            "FAULT_RESET_REARM_REQUIRED", "{\"powerOutputOn\":false,\"faultRecordPreserved\":true}", 0U);
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_LOG)
    {
        if (g_cloud.log_active)
        {
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
            return;
        }
        CloudMessages_PrepareLogSnapshot(request_id);
        if (!g_cloud.log_active)
        {
            (void)snprintf(result, result_size, "{\"lineCount\":0,\"dropped\":%lu}",
                           (unsigned long)AppLog_GetMqttDroppedCount());
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_REBOOT)
    {
        if (!boot_state_allows_reboot())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
        else
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "REBOOTING", "{}", 1U);
    }
    else if (action == CLOUD_PROTOCOL_ACTION_SET_MQTT || action == CLOUD_PROTOCOL_ACTION_SET_MQTT_PSK)
    {
        PatrolRemoteConfigV5_t candidate = *PatrolConfig_Get();
        ABoxMqttTrialState trial_state = MqttTrialManager_State();
        const ABoxTlsCredentials *credentials = PatrolConfig_MqttCredentials();
        if (action == CLOUD_PROTOCOL_ACTION_SET_MQTT_PSK) {
            const ABoxMqttConfig *active = MqttRuntime_ActiveConfig();
            if (!MqttRuntime_IsReady() || !active || !active->tls_enabled ||
                active->tls_credentials.mode != ABOX_TLS_AUTH_PSK ||
                request->mqtt_psk.generation <= credentials->generation) {
                CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "PSK_ROTATION_REJECTED");
                return;
            }
            credentials = &request->mqtt_psk;
        } else {
            if (!MqttRuntime_ActiveTls()) {
                CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "TLS_REQUIRED");
                return;
            }
            strcpy(candidate.vid, request->mqtt_vid);
            strcpy(candidate.mqtt_host, request->mqtt_host);
            candidate.mqtt_port = request->mqtt_port;
            strcpy(candidate.mqtt_username, request->mqtt_username);
            strcpy(candidate.mqtt_password, request->mqtt_password);
            candidate.mqtt_tls_enabled = request->mqtt_tls_enabled;
            candidate.mqtt_tls_profile_id = request->mqtt_tls_profile_id;
        }
        if (!boot_state_allows_reboot() || OtaService_IsBusy() ||
            g_cloud.mqtt_candidate_pending || g_cloud.mqtt_persistent_active ||
            (trial_state >= ABOX_MQTT_TRIAL_WAIT_ACCEPT && trial_state <= ABOX_MQTT_TRIAL_RESTORING) ||
            trial_state == ABOX_MQTT_TRIAL_RESTORE_FAILED) {
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
        } else if (!MqttTrialManager_StagePersistentPsk(&candidate, credentials, mqtt_trial_proof_id(request_id), CloudPort_Now())) {
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "RUNTIME_NOT_READY");
        } else {
            g_cloud.mqtt_candidate_pending = 1U;
            g_cloud.mqtt_persistent_active = 1U;
            CloudMessages_CopyString(g_cloud.mqtt_trial_request_id, sizeof(g_cloud.mqtt_trial_request_id), request_id);
            (void)snprintf(result, result_size, "{\"accepted\":true,\"flashWrite\":false}");
            if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 202U,
                                                "MQTT_CANDIDATE_TESTING", result, 0U)) {
                g_cloud.mqtt_candidate_pending = 0U;
                g_cloud.mqtt_persistent_active = 0U;
                MqttTrialManager_Cancel(CloudPort_Now());
            }
        }
    }
#if PATROL_CHASSIS_MQTT_LAB_TEST
    else if (action == CLOUD_PROTOCOL_ACTION_MQTT_LAB_SEED_CONTROL)
    {
        RemoteService_LabSeedNetworkState();
        if (!RemoteService_FormatNetworkState(result, result_size) ||
            !CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U,
                                            "LAB_CONTROL_STATE_SEEDED", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "LAB_STATE_ERROR");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_MQTT_LAB_MODEM_RESTART)
    {
        ABoxMqttTrialState trial_state = MqttTrialManager_State();
        if (g_cloud.modem_restart_pending || OtaService_IsBusy() ||
            (trial_state != ABOX_MQTT_TRIAL_IDLE && trial_state != ABOX_MQTT_TRIAL_COMMITTED &&
             trial_state != ABOX_MQTT_TRIAL_FAILED))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
        else if (CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 202U,
                                                "LAB_MODEM_RESTART_ACCEPTED", "{}", 0U))
            g_cloud.modem_restart_pending = 1U;
    }
#endif
    else if (action == CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_GET)
    {
        char remote[384];
        char session[21];
        mqtt_trial_session_decimal(MqttTrialManager_Session(), session);
        if (!RemoteService_FormatNetworkState(remote, sizeof(remote)))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "STATE_TOO_LARGE");
        else {
            (void)snprintf(result, result_size,
                "{\"runtimeState\":%u,\"ready\":%s,\"tls\":%s,\"client\":%u,\"context\":%u,\"trialState\":%u,\"trialSession\":%s,\"remote\":%s}",
                (unsigned)MqttRuntime_State(), MqttRuntime_IsReady() ? "true" : "false",
                MqttRuntime_ActiveTls() ? "true" : "false", (unsigned)MqttRuntime_ActiveClient(),
                (unsigned)MqttRuntime_ActiveContext(), (unsigned)MqttTrialManager_State(),
                session, remote);
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_CANDIDATE)
    {
#if PATROL_CHASSIS_MQTT_LAB_TEST
        ABoxMqttConfig candidate = {request->mqtt_host, request->mqtt_port, request->mqtt_username,
                                    request->mqtt_password, request->mqtt_tls_enabled,
                                    request->mqtt_tls_profile_id};
        if (g_cloud.mqtt_candidate_pending ||
            !MqttTrialManager_Stage(&candidate, mqtt_trial_proof_id(request_id), CloudPort_Now()))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "RUNTIME_NOT_READY");
        else {
            g_cloud.mqtt_candidate_pending = 1U;
            (void)snprintf(result, result_size,
                "{\"accepted\":true,\"tlsEnabled\":%s,\"tlsProfileId\":%u,\"flashWrite\":false}",
                request->mqtt_tls_enabled ? "true" : "false", (unsigned)request->mqtt_tls_profile_id);
            if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 202U,
                                                "RAM_CANDIDATE_ACCEPTED", result, 0U))
                g_cloud.mqtt_candidate_pending = 0U;
        }
#else
        CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 404U, "LAB_COMMAND_DISABLED");
#endif
    }
    else if (action == CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_CANCEL)
    {
#if PATROL_CHASSIS_MQTT_LAB_TEST
        ABoxMqttTrialState state = MqttTrialManager_State();
        if (state != ABOX_MQTT_TRIAL_VERIFYING || g_cloud.mqtt_cancel_pending)
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "TRIAL_NOT_CANCELLABLE");
        else {
            g_cloud.mqtt_cancel_pending = 1U;
            MqttTrialManager_DeferCancel();
            if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 202U,
                                                "RAM_TRIAL_CANCEL_ACCEPTED", "{}", 0U))
            {
                g_cloud.mqtt_cancel_pending = 0U;
                MqttTrialManager_Cancel(CloudPort_Now());
            }
        }
#else
        CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 404U, "LAB_COMMAND_DISABLED");
#endif
    }
}

// 校验并启动内部 OTA 维护请求。拒绝不兼容的升级状态，将下载、校验和安装准备交给 OTA 服务，不把请求接受当作升级成功。
static void handle_ota_request(const CloudProtocolRequest_t* request)
{
    BootStateRecord_t state;
    if (request == 0) return;
    if (g_cloud.mqtt_persistent_active) {
        CloudMessages_ScheduleSimpleError(g_cloud.topic_ota_resp, request->request_id, 409U, "MQTT_TRIAL_BUSY");
        return;
    }
    /* Same requestId is idempotent while the QoS1 response is still in flight. */
    if (g_cloud.ota_response_waiting) {
        if (strcmp(request->request_id, g_cloud.ota_request_id) == 0) return;
        CloudMessages_ScheduleSimpleError(g_cloud.topic_ota_resp, request->request_id, 409U, "DEVICE_BUSY");
        return;
    }
    if (BootState_Load(&state) && state.state == BOOT_STATE_CONFIRMED &&
        strcmp(request->ota_version, Patrol_AppFwVersion()) == 0) {
        (void)CloudMessages_ScheduleResponse(g_cloud.topic_ota_resp, request->request_id, 200U, "ALREADY_CONFIRMED",
                                "{\"otaPhase\":\"CONFIRMED\"}", 0U);
        return;
    }
    if (OtaService_IsBusy()) { CloudMessages_ScheduleSimpleError(g_cloud.topic_ota_resp, request->request_id, 409U, "DEVICE_BUSY"); return; }
    if (BootState_Load(&state) && (state.state != BOOT_STATE_NORMAL && state.state != BOOT_STATE_CONFIRMED)) {
        CloudMessages_ScheduleSimpleError(g_cloud.topic_ota_resp, request->request_id, 409U, "DEVICE_BUSY"); return;
    }
    if (!OtaService_IsReady()) { CloudMessages_ScheduleSimpleError(g_cloud.topic_ota_resp, request->request_id, 503U, "EC800_UNSUPPORTED"); return; }
    if (!OtaService_Start(request)) {
        uint32_t error = OtaService_LastError();
        uint16_t status = (error == OTA_ERROR_URL_REJECTED) ? 400U :
                          (error == OTA_ERROR_EC800_UNSUPPORTED || error == OTA_ERROR_CA_PROVISION_FAILED) ? 503U : 500U;
        char result[64];
        (void)snprintf(result, sizeof(result), "{\"errorCode\":%lu}", (unsigned long)error);
        (void)CloudMessages_ScheduleResponse(g_cloud.topic_ota_resp, request->request_id, status, "OTA_REQUEST_REJECTED", result, 0U);
        return;
    }
    CloudMessages_CopyString(g_cloud.ota_request_id, sizeof(g_cloud.ota_request_id), request->request_id);
    g_cloud.ota_response_waiting = 1U;
}

// 将公共控制请求交给底盘远控服务并调度响应。公共协议与内部维护协议保持分离，响应缓冲区受限；实际会话和安全仲裁由 RemoteService 完成。
static __attribute__((noinline)) void handle_public_control(const char* payload)
{
    /* Remote owns this workspace. ScheduleRawResponse copies it before a later
     * report reuses it; the transport also owns its own publish copy. */
    char *response = g_cloud.tx_payload;
    cJSON* root = cJSON_Parse(payload);
    cJSON* data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : 0;
    cJSON* command = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "command") : 0;
    cJSON* request_id = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "requestId") : 0;
    cJSON* params = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "params") : 0;
    cJSON* version = root ? cJSON_GetObjectItemCaseSensitive(root, "version") : 0;
    cJSON* timestamp = root ? cJSON_GetObjectItemCaseSensitive(root, "timestamp") : 0;
    cJSON* expires = data ? cJSON_GetObjectItemCaseSensitive(data, "expiresAt") : 0;
    if (cJSON_IsString(version) && strcmp(version->valuestring, "4.0") == 0 &&
        cJSON_IsString(command) && strcmp(command->valuestring, "sync_state") == 0 &&
        cJSON_IsString(request_id) && request_id->valuestring[0] != '\0' && cJSON_IsNull(params) &&
        cJSON_IsNumber(timestamp) && timestamp->valuedouble >= 0 &&
        timestamp->valuedouble <= 9007199254740991.0 &&
        timestamp->valuedouble == (double)(uint64_t)timestamp->valuedouble)
    {
        if (expires) {
            CloudMessages_ScheduleSimpleError(g_cloud.topic_control_resp, request_id->valuestring,
                                               4001U, "sync_state does not use expiresAt");
            cJSON_Delete(root); return;
        }
        /* get_info may already have reached the broker while its modem publish
         * acknowledgement is still pending. Keep the correlated refresh until
         * both the response slot and the previous status publish are free. */
        (void)ABoxCloudService_QueueSyncRequest(&g_cloud.cloud, request_id->valuestring);
        cJSON_Delete(root);
        return;
    }
    if (root) cJSON_Delete(root);
    if (RemoteService_HandleControl(payload, (uint16_t)strlen(payload), response, sizeof(g_cloud.tx_payload)))
        (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp, response);
    else APP_LOGW("REMOTE", "invalid or busy chassis control request");
}

// 按 Topic 分发收到的 MQTT 消息。优先识别时间同步、公共控制和内部维护入口，未知 Topic 不执行动作；解析结果仍须通过各业务门禁。
void CloudCommands_OnMessage(const char* topic, const char* payload)
{
    if (TimeSync_OnMessage(topic, payload)) return;
    /* MQTT callbacks are serialized by the Remote task and never re-enter. */
    static CloudProtocolRequest_t request;
    CloudProtocolStatus_t status;
    uint16_t payload_length;
    const char* response_topic;
    uint8_t ota_topic;

    static const ABoxCloudRoute routes[] = {
        {g_cloud.topic_control_cmd, 1U}, {g_cloud.topic_config_cmd, 2U}, {g_cloud.topic_ota_cmd, 3U}
    };
    ABoxCloudRequestContext context;
    if (!ABoxCloudDispatch_Open(routes, sizeof(routes) / sizeof(routes[0]), topic, payload, &context)) return;
    if (context.route == 1U)
    {
        handle_public_control(payload);
        return;
    }
    if (context.route != 2U && context.route != 3U) return;
    ota_topic = context.route == 3U;
    response_topic = ota_topic ? g_cloud.topic_ota_resp : g_cloud.topic_config_resp;
    payload_length = (uint16_t)strlen(payload);
    status = CloudProtocol_Parse(payload, payload_length, ota_topic, &request);
    if (status != CLOUD_PROTOCOL_OK)
    {
        if (request.request_id[0] != '\0')
            CloudMessages_ScheduleSimpleError(response_topic, request.request_id, (uint16_t)status,
                                  CloudProtocol_StatusMessage(status));
        else APP_LOGW("CLOUD", "uncorrelated invalid request code=%u", (unsigned int)status);
        return;
    }
    if (g_cloud.mqtt_persistent_active &&
        strcmp(request.request_id, g_cloud.mqtt_trial_request_id) == 0) return;
    if (strcmp(request.request_id, g_cloud.last_request_id) == 0 && g_cloud.last_response_payload[0] != '\0')
    {
        if (!g_cloud.cloud.response_pending)
        {
            CloudMessages_CopyString(g_cloud.response_topic, sizeof(g_cloud.response_topic), g_cloud.last_response_topic);
            CloudMessages_CopyString(g_cloud.response_payload, sizeof(g_cloud.response_payload), g_cloud.last_response_payload);
            (void)ABoxCloudService_QueueResponse(&g_cloud.cloud, request.request_id, 0);
            g_cloud.response_reboots = 0U;
        }
        return;
    }
    if (g_cloud.cloud.response_pending || g_cloud.reboot_pending)
    {
        APP_LOGW("CLOUD", "command busy");
        return;
    }
    if (g_cloud.log_active)
    {
        if (strcmp(request.request_id, g_cloud.log_request_id) != 0)
            CloudMessages_ScheduleSimpleError(response_topic, request.request_id, 409U, "DEVICE_BUSY");
        return;
    }
    if (ota_topic) handle_ota_request(&request);
    else handle_config_request(&request);
}
