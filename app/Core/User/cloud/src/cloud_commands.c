#include "cloud_internal.h"
#include "cJSON.h"
#include "time_base.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_version.h"
#include "boot_state.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "ota_service.h"
#include "remote_service.h"
#include "patrol_config.h"
#include "patrol_runtime.h"
#include "center_calibration.h"
#include "patrol_can_status.h"
#include "vehicle_power_output.h"

static uint8_t boot_state_allows_reboot(void)
{
    BootStateRecord_t state;
    if (!BootState_Load(&state)) return 1U;
    return (state.state == BOOT_STATE_NORMAL || state.state == BOOT_STATE_CONFIRMED) ? 1U : 0U;
}
#if PATROL_CHASSIS_MQTT_LAB_TEST
static uint64_t mqtt_trial_proof_id(const char *text)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    while (text != 0 && *text != '\0') { hash ^= (uint8_t)*text++; hash *= UINT64_C(1099511628211); }
    return hash != 0U ? hash : 1U;
}
#endif
static void mqtt_trial_session_decimal(uint64_t value, char output[21])
{
    char reverse[20];
    unsigned count = 0U, i;
    do { reverse[count++] = (char)('0' + (value % 10U)); value /= 10U; } while (value != 0U);
    for (i = 0U; i < count; ++i) output[i] = reverse[count - i - 1U];
    output[count] = '\0';
}

static uint32_t signed_delta_mdeg(int64_t delta, uint32_t counts_per_degree_milli)
{
    uint64_t magnitude;
    uint64_t result;
    if (counts_per_degree_milli == 0U) return UINT32_MAX;
    magnitude = (uint64_t)(delta < 0 ? -delta : delta);
    result = magnitude * 1000000ULL / counts_per_degree_milli;
    return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

static uint8_t calibration_feedback_ready(const CloudProtocolRequest_t* request,
                                          const PatrolDiagnosticsSnapshot* snapshot)
{
    uint32_t steering_offset;
    uint32_t encoder_offset;
    const ChassisControl* control = RemoteService_State();
    if (request == 0 || snapshot == 0 || control == 0) return 0U;
    if (!VehiclePowerOutput_IsOn() || PatrolRtos_CanBusOff() || PatrolRtos_CanBusOffActive() ||
        !snapshot->steering_online || !snapshot->drive_online ||
        !snapshot->steering_feedback_fresh || !snapshot->drive_feedback_fresh ||
        !snapshot->encoder_online || !snapshot->encoder_config_valid || snapshot->drive_fault ||
        labs(snapshot->left_actual_rpm) > 20L || labs(snapshot->right_actual_rpm) > 20L ||
        control->remote_enabled || control->session_active || control->enable_request ||
        (snapshot->steering_status_word & 0x006FU) == 0x0027U ||
        (snapshot->drive_status_word & 0x006FU) == 0x0027U) return 0U;
    steering_offset = signed_delta_mdeg(
        (int64_t)snapshot->steering_position_count - (int64_t)request->steering_center_count,
        request->steering_counts_per_degree_milli);
    encoder_offset = signed_delta_mdeg(
        (int64_t)snapshot->encoder_count - (int64_t)request->absolute_center_count,
        request->absolute_counts_per_degree_milli);
    return (uint8_t)(steering_offset <= request->steering_tolerance_mdeg &&
                     encoder_offset <= request->steering_tolerance_mdeg);
}

static __attribute__((noinline)) void handle_config_request(const CloudProtocolRequest_t* request)
{
    /* Remote task is the sole caller. Keep Flash-save scratch off its nested MQTT stack. */
    static char result[900];
    CloudProtocolAction_t action;
    const char* request_id;
    if (request == 0) return;
    action = request->action;
    request_id = request->request_id;
    /* A maintenance query opens a bounded observation window, never a motion permit. */
    CloudTelemetry_Wake(&g_cloud.telemetry, CloudPort_Now());
    if (action == CLOUD_PROTOCOL_ACTION_GET_INFO)
    {
        CloudMessages_BuildInfoResult(result, sizeof(result));
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
                PatrolRtos_CanRawRxCount(0U), PatrolRtos_CanRawRxCount(1U), result, sizeof(result)))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "DIAGNOSTIC_TOO_LARGE");
        else if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_CALIBRATION)
    {
        CloudMessages_BuildCalibrationResult(result, sizeof(result));
        if (!CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_CLEAR_CALIBRATION)
    {
        if (OtaService_IsBusy())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "OTA_BUSY");
        else if (RemoteService_BenchPermitted())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "BENCH_ACTIVE");
        else if (!PatrolConfig_ClearCalibration())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "CONFIG_WRITE_FAILED");
        else
        {
            CloudMessages_BuildCalibrationResult(result, sizeof(result));
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U,
                                    "CALIBRATION_CLEARED", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_SET_CALIBRATION)
    {
        PatrolDiagnosticsSnapshot snapshot;
        PatrolRuntime_GetDiagnostics(&snapshot);
        if (OtaService_IsBusy())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "OTA_BUSY");
        else if (RemoteService_BenchPermitted() || !calibration_feedback_ready(request, &snapshot))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U,
                                  "CALIBRATION_FEEDBACK_NOT_READY");
        else if (!PatrolConfig_SetCalibration(request->steering_center_count,
                    request->absolute_center_count, request->steering_counts_per_degree_milli,
                    request->absolute_counts_per_degree_milli, request->steering_direction,
                    request->absolute_direction, request->steering_tolerance_mdeg))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "CONFIG_WRITE_FAILED");
        else
        {
            CloudMessages_BuildCalibrationResult(result, sizeof(result));
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U,
                                    "CALIBRATION_SAVED_REMOTE_DISABLED", result, 0U);
        }
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
            if (!CenterCalibration_Format(result, sizeof(result)) ||
                !CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 409U,
                                   "CENTER_CALIBRATION_NOT_READY", result, 0U))
                CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U,
                                      "CENTER_CALIBRATION_NOT_READY");
        } else {
            if (!CenterCalibration_Format(result, sizeof(result)) ||
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
    else if (action == CLOUD_PROTOCOL_ACTION_BENCH_ARM || action == CLOUD_PROTOCOL_ACTION_BENCH_STOP ||
             action == CLOUD_PROTOCOL_ACTION_BENCH_GET)
    {
        if (action == CLOUD_PROTOCOL_ACTION_BENCH_ARM && !(request->bench_drive ? PatrolRuntime_BenchDriveArm(request->bench_limit_mdeg) :
                PatrolRuntime_BenchArm(request->bench_limit_mdeg)))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "BENCH_NOT_READY");
        else {
            if (action == CLOUD_PROTOCOL_ACTION_BENCH_STOP) PatrolRuntime_BenchStop();
            PatrolRuntime_FormatBench(result, sizeof(result));
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "OK", result, 0U);
        }
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
            (void)snprintf(result, sizeof(result), "{\"lineCount\":0,\"dropped\":%lu}",
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
    else if (action == CLOUD_PROTOCOL_ACTION_POWER_ON || action == CLOUD_PROTOCOL_ACTION_POWER_OFF)
    {
        uint8_t target_on = action == CLOUD_PROTOCOL_ACTION_POWER_ON ? 1U : 0U;
        if (!VehiclePowerOutput_IsAvailable())
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 503U, "POWER_OUTPUT_UNAVAILABLE");
        else if (!VehiclePowerOutput_Set(target_on))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "POWER_OUTPUT_WRITE_FAILED");
        else
        {
            (void)snprintf(result, sizeof(result), "{\"powerOutputOn\":%s}",
                           target_on ? "true" : "false");
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U,
                                    target_on ? "POWER_ON" : "POWER_OFF", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_SET_MQTT)
    {
        /* Reply on the old VID topic before reset; the next heartbeat carries the new identity. */
        if (!boot_state_allows_reboot()) {
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 409U, "DEVICE_BUSY");
        } else if (!PatrolConfig_SetMqtt(request->mqtt_vid, request->mqtt_host, request->mqtt_port,
                                      request->mqtt_username, request->mqtt_password)) {
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "CONFIG_WRITE_FAILED");
        } else {
            (void)snprintf(result, sizeof(result), "{\"vid\":\"%s\",\"rebooting\":true}", request->mqtt_vid);
            (void)CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U, "MQTT_CONFIG_SAVED", result, 1U);
        }
    }
#if PATROL_CHASSIS_MQTT_LAB_TEST
    else if (action == CLOUD_PROTOCOL_ACTION_MQTT_LAB_SEED_CONTROL)
    {
        RemoteService_LabSeedNetworkState();
        if (!RemoteService_FormatNetworkState(result, sizeof(result)) ||
            !CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, request_id, 200U,
                                            "LAB_CONTROL_STATE_SEEDED", result, 0U))
            CloudMessages_ScheduleSimpleError(g_cloud.topic_config_resp, request_id, 500U, "LAB_STATE_ERROR");
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
            (void)snprintf(result, sizeof(result),
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
            (void)snprintf(result, sizeof(result),
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

static void handle_ota_request(const CloudProtocolRequest_t* request)
{
    BootStateRecord_t state;
    if (request == 0) return;
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

static __attribute__((noinline)) void handle_public_control(const char* payload)
{
    char response[CLOUD_PAYLOAD_SIZE];
    cJSON* root = cJSON_Parse(payload);
    cJSON* data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : 0;
    cJSON* command = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "command") : 0;
    cJSON* request_id = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "requestId") : 0;
    cJSON* params = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "params") : 0;
    cJSON* version = root ? cJSON_GetObjectItemCaseSensitive(root, "version") : 0;
    if (cJSON_IsString(version) && strcmp(version->valuestring, "4.0") == 0 &&
        cJSON_IsString(command) && strcmp(command->valuestring, "sync_state") == 0 &&
        cJSON_IsString(request_id) && request_id->valuestring[0] != '\0' && cJSON_IsNull(params))
    {
        char timestamp[21];
        if (!TimeBase_FormatUtcMs(timestamp, sizeof(timestamp))) strcpy(timestamp, "0");
        (void)snprintf(response, sizeof(response),
            "{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{\"requestId\":\"%s\","
            "\"code\":200,\"msg\":\"state synchronization scheduled\","
            "\"result\":{\"expectedStates\":[{\"reportName\":\"chassis_status\"}]}}}",
            timestamp, request_id->valuestring);
        CloudMessages_CopyString(g_cloud.status_report_request_id,
                                 sizeof(g_cloud.status_report_request_id), request_id->valuestring);
        g_cloud.status_report_due = CloudPort_Now();
        (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp, response);
        cJSON_Delete(root);
        return;
    }
    if (root) cJSON_Delete(root);
    if (RemoteService_HandleControl(payload, (uint16_t)strlen(payload), response, sizeof(response)))
        (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp, response);
    else APP_LOGW("REMOTE", "invalid or busy chassis control request");
}

void CloudCommands_OnMessage(const char* topic, const char* payload)
{
    /* MQTT callbacks are serialized by the Remote task and never re-enter. */
    static CloudProtocolRequest_t request;
    CloudProtocolStatus_t status;
    uint16_t payload_length;
    const char* response_topic;
    uint8_t ota_topic;

    if ((topic == 0) || (payload == 0)) return;
    if (strcmp(topic, g_cloud.topic_control_cmd) == 0)
    {
        handle_public_control(payload);
        return;
    }
    if ((strcmp(topic, g_cloud.topic_config_cmd) != 0) && (strcmp(topic, g_cloud.topic_ota_cmd) != 0)) return;
    ota_topic = (strcmp(topic, g_cloud.topic_ota_cmd) == 0) ? 1U : 0U;
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
    if (strcmp(request.request_id, g_cloud.last_request_id) == 0 && g_cloud.last_response_payload[0] != '\0')
    {
        if (!g_cloud.response_pending)
        {
            CloudMessages_CopyString(g_cloud.response_topic, sizeof(g_cloud.response_topic), g_cloud.last_response_topic);
            CloudMessages_CopyString(g_cloud.response_payload, sizeof(g_cloud.response_payload), g_cloud.last_response_payload);
            CloudMessages_CopyString(g_cloud.response_request_id, sizeof(g_cloud.response_request_id), request.request_id);
            g_cloud.response_pending = 1U;
            g_cloud.response_reboots = 0U;
        }
        return;
    }
    if (g_cloud.response_pending || g_cloud.reboot_pending)
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
