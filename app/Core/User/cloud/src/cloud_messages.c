#include "cloud_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_version.h"
#include "abox_boot_v2.h"
#include "abox_boot_v2_app.h"
#include "boot_state.h"
#include "fixed_point.h"
#include "mqtt_core.h"
#include "ota_service.h"
#include "remote_service.h"
#include "time_base.h"
#include "patrol_config.h"
#include "patrol_runtime.h"
#include "patrol_can_status.h"
#include "vehicle_power_output.h"
#include "abox_mqtt_v4.h"
#include "patrol_mqtt_profile.h"

void CloudMessages_CopyString(char* dst, uint16_t size, const char* src)
{
    if ((dst == 0) || (size == 0U)) return;
    if (src == 0) src = "";
    strncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

static void json_init(JsonWriter_t* writer, char* buffer, uint16_t capacity)
{
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0U;
    writer->valid = (buffer != 0 && capacity > 1U) ? 1U : 0U;
    if (writer->valid) buffer[0] = '\0';
}

static void json_append(JsonWriter_t* writer, const char* text)
{
    uint16_t length;
    if ((writer == 0) || !writer->valid || (text == 0)) return;
    length = (uint16_t)strlen(text);
    if ((uint32_t)writer->length + length >= writer->capacity) { writer->valid = 0U; return; }
    memcpy(writer->buffer + writer->length, text, length + 1U);
    writer->length = (uint16_t)(writer->length + length);
}

static void json_appendf(JsonWriter_t* writer, const char* fmt, ...)
{
    va_list ap;
    int written;
    if ((writer == 0) || !writer->valid || (fmt == 0)) return;
    va_start(ap, fmt);
    written = vsnprintf(writer->buffer + writer->length, writer->capacity - writer->length, fmt, ap);
    va_end(ap);
    if ((written < 0) || ((uint32_t)writer->length + (uint32_t)written >= writer->capacity))
    {
        writer->valid = 0U;
        return;
    }
    writer->length = (uint16_t)(writer->length + (uint16_t)written);
}

static void json_string_n(JsonWriter_t* writer, const char* value, uint16_t length)
{
    uint16_t i;
    char escape[7];
    json_append(writer, "\"");
    for (i = 0U; i < length && writer->valid; i++)
    {
        uint8_t ch = (uint8_t)value[i];
        switch (ch)
        {
            case '"': json_append(writer, "\\\""); break;
            case '\\': json_append(writer, "\\\\"); break;
            case '\b': json_append(writer, "\\b"); break;
            case '\f': json_append(writer, "\\f"); break;
            case '\n': json_append(writer, "\\n"); break;
            case '\r': json_append(writer, "\\r"); break;
            case '\t': json_append(writer, "\\t"); break;
            default:
                if (ch < 0x20U)
                {
                    (void)snprintf(escape, sizeof(escape), "\\u%04X", (unsigned int)ch);
                    json_append(writer, escape);
                }
                else
                {
                    char one[2] = {(char)ch, '\0'};
                    json_append(writer, one);
                }
                break;
        }
    }
    json_append(writer, "\"");
}

static void json_string(JsonWriter_t* writer, const char* value)
{
    json_string_n(writer, value ? value : "", (uint16_t)strlen(value ? value : ""));
}

static uint16_t escaped_length(const char* value, uint16_t length)
{
    uint16_t i;
    uint16_t result = 2U;
    for (i = 0U; i < length; i++)
    {
        uint8_t ch = (uint8_t)value[i];
        if ((ch == '"') || (ch == '\\') || (ch == '\b') || (ch == '\f') || (ch == '\n') ||
            (ch == '\r') || (ch == '\t')) result += 2U;
        else if (ch < 0x20U) result += 6U;
        else result++;
    }
    return result;
}

static void json_append_timestamp(JsonWriter_t* writer)
{
    char value[21];
    if (!TimeBase_FormatUtcMs(value, sizeof(value)))
    {
        json_append(writer, "0");
        return;
    }
    json_append(writer, value);
}

static const char* boot_state_name(uint32_t state)
{
    switch ((BootStateCode_t)state)
    {
        case BOOT_STATE_INSTALL_PENDING: return "INSTALL_PENDING";
        case BOOT_STATE_INSTALLING: return "INSTALLING";
        case BOOT_STATE_TRIAL: return "TRIAL";
        case BOOT_STATE_CONFIRMED: return "CONFIRMED";
        case BOOT_STATE_ROLLBACK: return "ROLLBACK";
        case BOOT_STATE_NORMAL:
        default: return "NORMAL";
    }
}

static const char* mqtt_state_name(MQTT_CoreState_t state)
{
    if (state == MQTT_CORE_ST_READY) return "READY";
    if (state == MQTT_CORE_ST_RECONNECT_WAIT || state == MQTT_CORE_ST_RECOVER_PUB) return "RECONNECTING";
    if (state == MQTT_CORE_ST_ERROR) return "ERROR";
    if (state == MQTT_CORE_ST_IDLE || state == MQTT_CORE_ST_WAIT_BOOT) return "STARTING";
    return "CONNECTING";
}

void CloudMessages_CacheResponse(const char* request_id, const char* topic, const char* payload)
{
    CloudMessages_CopyString(g_cloud.last_request_id, sizeof(g_cloud.last_request_id), request_id);
    CloudMessages_CopyString(g_cloud.last_response_topic, sizeof(g_cloud.last_response_topic), topic);
    CloudMessages_CopyString(g_cloud.last_response_payload, sizeof(g_cloud.last_response_payload), payload);
}

uint8_t CloudMessages_ScheduleResponse(const char* topic, const char* request_id, uint16_t code, const char* message,
                                 const char* result_json, uint8_t reboot)
{
    JsonWriter_t writer;
    if (g_cloud.response_pending) return 0U;
    json_init(&writer, g_cloud.response_payload, sizeof(g_cloud.response_payload));
    json_append(&writer, "{\"version\":\"2.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_append(&writer, ",\"data\":{\"product\":\"patrol_vehicle_chassis\",\"requestId\":");
    json_string(&writer, request_id);
    json_appendf(&writer, ",\"code\":%u,\"msg\":", (unsigned int)code);
    json_string(&writer, message);
    json_append(&writer, ",\"result\":");
    json_append(&writer, result_json ? result_json : "{}");
    json_append(&writer, "}}");
    if (!writer.valid || writer.length >= CLOUD_PAYLOAD_LIMIT) return 0U;
    CloudMessages_CopyString(g_cloud.response_topic, sizeof(g_cloud.response_topic), topic);
    CloudMessages_CopyString(g_cloud.response_request_id, sizeof(g_cloud.response_request_id), request_id);
    g_cloud.response_pending = 1U;
    g_cloud.response_reboots = reboot;
    return 1U;
}

void CloudMessages_ScheduleSimpleError(const char* topic, const char* request_id, uint16_t code, const char* message)
{
    if (!CloudMessages_ScheduleResponse(topic, request_id, code, message, "{}", 0U))
        APP_LOGW("CLOUD", "response queue busy code=%u", (unsigned int)code);
}

uint8_t CloudMessages_ScheduleRawResponse(const char* topic, const char* payload)
{
    if (g_cloud.response_pending || topic == 0 || payload == 0 || strlen(payload) >= sizeof(g_cloud.response_payload)) return 0U;
    CloudMessages_CopyString(g_cloud.response_topic, sizeof(g_cloud.response_topic), topic);
    CloudMessages_CopyString(g_cloud.response_payload, sizeof(g_cloud.response_payload), payload);
    g_cloud.response_request_id[0] = '\0';
    g_cloud.response_pending = 1U;
    g_cloud.response_reboots = 0U;
    return 1U;
}

void CloudMessages_BuildInfoResult(char* output, uint16_t size)
{
    JsonWriter_t writer;
    BootStateRecord_t state;
    ABoxBootV2Descriptor descriptor;
    uint8_t have_state = BootState_Load(&state);
    uint8_t descriptor_valid = (uint8_t)(ABoxBootV2_DescriptorRead(&descriptor) &&
                                         ABoxBootV2_DescriptorValid(&descriptor));
    const char* iccid = MQTT_Core_GetIccid();
    PatrolDiagnosticsSnapshot diagnostics;
    PatrolRuntime_GetDiagnostics(&diagnostics);
    json_init(&writer, output, size);
    json_append(&writer, "{\"fwVersion\":"); json_string(&writer, Patrol_AppFwVersion());
    json_append(&writer, ",\"bootVersion\":"); json_string(&writer, descriptor_valid ? descriptor.build_version : "");
    json_appendf(&writer, ",\"bootDescriptorValid\":%s,\"bootApiVersion\":%u,\"bootFeatures\":%lu",
                 descriptor_valid ? "true" : "false", descriptor_valid ? descriptor.abi_version : 0U,
                 (unsigned long)(descriptor_valid ? descriptor.feature_flags : 0U));
    json_append(&writer, ",\"otaScheme\":\"boot_v2_ufs\",\"otaDownloader\":\"app\"");
    json_append(&writer, ",\"stableSeedSource\":");
    json_string(&writer, ABoxBootV2App_StableSeedSource());
    json_append(&writer, ",\"otaBootstrapNeedsRevision\":false");
    json_append(&writer, ",\"vid\":"); json_string(&writer, PatrolConfig_Get()->vid);
    json_append(&writer, ",\"mqttHost\":"); json_string(&writer, PatrolConfig_Get()->mqtt_host);
    json_appendf(&writer, ",\"mqttPort\":%u,\"mqttTls\":%s,\"mqttTlsProfileId\":%u",
                 (unsigned)PatrolConfig_Get()->mqtt_port,
                 PatrolConfig_Get()->mqtt_tls_enabled ? "true" : "false",
                 (unsigned)PatrolConfig_Get()->mqtt_tls_profile_id);
    json_appendf(&writer, ",\"uptimeMs\":%lu,\"mqttState\":", (unsigned long)CloudPort_Now());
    json_string(&writer, mqtt_state_name(MQTT_Core_GetState()));
    json_appendf(&writer, ",\"mqttStateCode\":%u,\"iccid\":", (unsigned int)MQTT_Core_GetState());
    json_string(&writer, iccid ? iccid : "");
    json_appendf(&writer,
                 ",\"canInitialized\":%s,\"canBusOff\":%s,\"canRxOverflow\":%lu,"
                 "\"boDiag\":[%lu,%u],\"otaState\":",
                 PatrolRtos_CanStarted() ? "true" : "false", PatrolRtos_CanBusOff() ? "true" : "false",
                 (unsigned long)PatrolRtos_CanRxOverflowCount(),
                 (unsigned long)PatrolRtos_CanLastBusOffMs(), (unsigned)PatrolRtos_CanRecoverySamples());
    json_string(&writer, (have_state && state.rollback_report_pending) ? "ROLLBACK" :
                boot_state_name(have_state ? state.state : BOOT_STATE_NORMAL));
    json_appendf(&writer, ",\"powerOutputAvailable\":%s,\"powerOutputOn\":%s",
                 VehiclePowerOutput_IsAvailable() ? "true" : "false",
                 VehiclePowerOutput_IsOn() ? "true" : "false");
    json_appendf(&writer, ",\"stableSlot\":%u,\"candidateSlot\":%u,\"trialFailCount\":%u,"
                 "\"resetReason\":%u,\"otaProvisioned\":%s,\"otaReady\":%s,\"otaBusy\":%s,\"otaPhase\":",
                 have_state ? state.stable_slot : 0U, have_state ? state.candidate_slot : 0U,
                 have_state ? state.trial_fail_count : 0U, (unsigned int)g_cloud.reset_reason,
                 OtaService_IsProvisioned() ? "true" : "false",
                 OtaService_IsReady() ? "true" : "false", OtaService_IsBusy() ? "true" : "false");
    json_string(&writer, OtaService_Phase());
    json_append(&writer, ",\"otaTargetVersion\":");
    json_string(&writer, OtaService_IsBusy() ? OtaService_TargetVersion() : (have_state ? state.image_version : ""));
    json_appendf(&writer, ",\"lastOtaError\":%lu,\"diagnostics\":{\"canRx\":%lu,"
                 "\"steerHb\":%s,\"driveHb\":%s,\"encoderOnline\":%s}}",
                 (unsigned long)(OtaService_LastError() ? OtaService_LastError() :
                                 (have_state ? state.last_error : 0U)),
                 (unsigned long)diagnostics.can_rx_count,
                 diagnostics.steering_online ? "true" : "false",
                 diagnostics.drive_online ? "true" : "false",
                 diagnostics.encoder_online ? "true" : "false");
    if (!writer.valid) CloudMessages_CopyString(output, size, "{}");
}

uint8_t CloudMessages_BuildHeartbeat(char* output, uint16_t size)
{
    JsonWriter_t writer;
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"4.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_append(&writer, ",\"data\":{\"firmware\":"); json_string(&writer, Patrol_AppFwVersion());
    json_append(&writer, "}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

uint8_t CloudMessages_BuildManifest(char* output, uint16_t size)
{
    JsonWriter_t writer;
    const ABoxMqttProfileDescriptor* profile = PatrolMqttProfile_Get();
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"4.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_append(&writer, ",\"data\":{\"firmware\":"); json_string(&writer, Patrol_AppFwVersion());
    json_append(&writer, ",\"profiles\":[{\"name\":"); json_string(&writer, profile->name);
    json_append(&writer, ",\"version\":"); json_string(&writer, profile->version);
    json_append(&writer, "}]}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

void CloudMessages_RememberStatusSnapshot(uint32_t now)
{
    const ChassisControl* control = RemoteService_State();
    g_cloud.status_snapshot.valid = 1U;
    g_cloud.status_snapshot.safety_ready = ChassisControl_SafetyReady(control, now);
    g_cloud.status_snapshot.fault_latched = control->fault_latched;
    g_cloud.status_snapshot.reversal_pending = control->reversal_pending;
    g_cloud.status_snapshot.power_output_on = VehiclePowerOutput_IsOn();
    g_cloud.status_snapshot.stop_reason = (uint8_t)control->stop_reason;
    g_cloud.status_snapshot.target_rpm = control->target_rpm;
    g_cloud.status_snapshot.target_steering_mdeg = control->target_steering_mdeg;
}

const char* CloudMessages_StatusChangeReason(uint32_t now)
{
    const ChassisControl* control = RemoteService_State();
    uint8_t safety_ready;
    if (!g_cloud.status_snapshot.valid) return 0;
    safety_ready = ChassisControl_SafetyReady(control, now);
    if (control->fault_latched != g_cloud.status_snapshot.fault_latched)
        return control->fault_latched ? "fault_raised" : "fault_cleared";
    if ((g_cloud.status_snapshot.safety_ready && !safety_ready) ||
        (g_cloud.status_snapshot.target_rpm != 0 && control->target_rpm == 0 &&
         control->stop_reason != CHASSIS_STOP_NONE))
        return "safety_stop";
    if (safety_ready != g_cloud.status_snapshot.safety_ready ||
        VehiclePowerOutput_IsOn() != g_cloud.status_snapshot.power_output_on ||
        control->reversal_pending != g_cloud.status_snapshot.reversal_pending ||
        control->stop_reason != (ChassisStopReason)g_cloud.status_snapshot.stop_reason ||
        control->target_rpm != g_cloud.status_snapshot.target_rpm ||
        control->target_steering_mdeg != g_cloud.status_snapshot.target_steering_mdeg)
        return "state_changed";
    return 0;
}

uint8_t CloudMessages_BuildStatusReport(char* output, uint16_t size, const char* report_reason)
{
    JsonWriter_t writer;
    const ChassisControl* control = RemoteService_State();
    const ChassisFeedback* feedback = &control->feedback;
    uint32_t now = CloudPort_Now();
    uint8_t feedback_fresh = (uint8_t)((now - feedback->feedback_ms) <= CHASSIS_FEEDBACK_TIMEOUT_MS);
    const char* motion_state = "stopped";
    char timestamp[21];
    char report_id[96];
    char target_steering[24];
    char actual_steering[24];
    char actual_encoder[24];
    PatrolDiagnosticsSnapshot diagnostics;
    MQTT_CoreRxDiagnostics_t mqtt_rx;
    MotionFaultRecord fault;
    uint8_t have_fault=PatrolRuntime_GetFault(&fault);
    if (control->fault_latched) motion_state = "fault";
    else if (control->reversal_pending) motion_state = "reversing_wait";
    else if (feedback_fresh &&
             (abs(feedback->left_actual_rpm) > 20 || abs(feedback->right_actual_rpm) > 20))
        motion_state = "driving";
    if (!TimeBase_FormatUtcMs(timestamp, sizeof(timestamp))) strcpy(timestamp, "0");
    (void)snprintf(report_id, sizeof(report_id), "%s-%s-%lu", PatrolConfig_Get()->vid,
                   timestamp, (unsigned long)g_cloud.status_report_sequence++);
    if (!FixedPoint_FormatMilli(target_steering, sizeof(target_steering),
                                control->target_steering_mdeg) ||
        !FixedPoint_FormatMilli(actual_steering, sizeof(actual_steering),
                                feedback->steering_actual_mdeg) ||
        !FixedPoint_FormatMilli(actual_encoder, sizeof(actual_encoder),
                                feedback->encoder_actual_mdeg)) return 0U;
    PatrolRuntime_GetDiagnostics(&diagnostics);
    MQTT_Core_GetRxDiagnostics(&mqtt_rx);
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"4.0\",\"timestamp\":");
    json_append(&writer, timestamp);
    json_append(&writer, ",\"data\":{\"reportId\":");
    json_string(&writer, report_id);
    json_append(&writer, ",\"reportType\":\"state\",\"reportName\":\"chassis_status\"");
    if (g_cloud.status_report_request_id[0] != '\0')
    {
        json_append(&writer, ",\"requestId\":");
        json_string(&writer, g_cloud.status_report_request_id);
    }
    json_append(&writer, ",\"payload\":{\"reportReason\":");
    json_string(&writer, report_reason ? report_reason : "periodic");
    json_append(&writer, ",\"motionState\":");
    json_string(&writer, motion_state);
    json_appendf(&writer,
        ",\"targetDriveRpm\":%d,\"targetSteeringDeg\":%s,\"leftActualRpm\":%d,"
        "\"rightActualRpm\":%d,\"steeringActualDeg\":%s,\"absoluteActualDeg\":%s,"
        "\"steeringOnline\":%s,\"driveOnline\":%s,\"encoderOnline\":%s,"
        "\"canBusOff\":%s,\"driveFault\":%s,\"feedbackFresh\":%s,"
        "\"calibrated\":%s,\"steeringProfileReady\":%s,\"steeringRangeValid\":%s,"
        "\"powerReady\":%s,\"powerOutputOn\":%s,\"otaBusy\":%s,\"safetyReady\":%s,"
        "\"stopReason\":%u,\"faultLatched\":%s,"
        "\"steeringBoundary\":%d,\"faultRecordVersion\":1",
        (int)control->target_rpm, target_steering,
        (int)feedback->left_actual_rpm, (int)feedback->right_actual_rpm,
        actual_steering, actual_encoder,
        feedback->steering_online ? "true" : "false",
        feedback->drive_online ? "true" : "false",
        diagnostics.encoder_online ? "true" : "false",
        feedback->can_bus_off ? "true" : "false",
        feedback->drive_fault ? "true" : "false", feedback_fresh ? "true" : "false",
        feedback->calibrated ? "true" : "false",
        feedback->encoder_profile_ready ? "true" : "false",
        feedback->encoder_range_valid ? "true" : "false",
        feedback->power_ready ? "true" : "false",
        VehiclePowerOutput_IsOn() ? "true" : "false",
        feedback->ota_busy ? "true" : "false",
        ChassisControl_SafetyReady(control, now) ? "true" : "false",
        (unsigned int)control->stop_reason, control->fault_latched ? "true" : "false", (int)control->steering_boundary);
    if (have_fault) {
        const uint32_t* words=(const uint32_t*)&fault;
        json_append(&writer, ",\"lastFault\":[");
        /* Word order is frozen by faultRecordVersion, signed words 6..10. */
        for (unsigned i=1;i<=28;i++) {
            if (i>1) json_append(&writer, ",");
            if (i>=6 && i<=10) json_appendf(&writer,"%ld",(long)(int32_t)words[i]);
            else json_appendf(&writer,"%lu",(unsigned long)words[i]);
        }
        json_appendf(&writer, ",%u]", (unsigned)PatrolRuntime_FaultStorage());
    } else {
        json_appendf(&writer,
            ",\"transportDiagnostics\":{\"mqttRxLines\":%lu,\"mqttRxDecoded\":%lu,"
            "\"mqttRxRejected\":%lu,\"mqttRxDispatched\":%lu,\"mqttRxOverwrites\":%lu,"
            "\"mqttRxTimeouts\":%lu,\"mqttRxLastMs\":%lu,\"ecDmaOverflows\":%lu,"
            "\"ecDmaRestarts\":%lu,\"ecDmaMaxBacklog\":%lu,\"ecLineOverflows\":%lu}",
            (unsigned long)mqtt_rx.qmtrecv_line_count, (unsigned long)mqtt_rx.decoded_message_count,
            (unsigned long)mqtt_rx.rejected_frame_count, (unsigned long)mqtt_rx.dispatched_message_count,
            (unsigned long)mqtt_rx.pending_overwrite_count, (unsigned long)mqtt_rx.multiline_timeout_count,
            (unsigned long)mqtt_rx.last_qmtrecv_tick, (unsigned long)ABoxEc800Rx_OverflowCount(&g_cloud.ec800_rx),
            (unsigned long)ABoxEc800Rx_RestartCount(&g_cloud.ec800_rx),
            (unsigned long)ABoxEc800Rx_MaxBacklog(&g_cloud.ec800_rx), (unsigned long)EC800_AT_LineOverflowCount(&g_cloud.at_core));
    }
    json_append(&writer,"}}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

void CloudMessages_PrepareLogSnapshot(const char* request_id)
{
    uint16_t length;
    uint16_t pos = 0U;
    uint8_t i;
    uint8_t chunk = 0U;
    uint16_t estimated = 220U;

    CloudMessages_CopyString(g_cloud.log_request_id, sizeof(g_cloud.log_request_id), request_id);
    length = AppLog_Dump(g_cloud.log_snapshot, sizeof(g_cloud.log_snapshot));
    g_cloud.log_line_count = 0U;
    while ((pos < length) && (g_cloud.log_line_count < CLOUD_LOG_MAX_LINES))
    {
        uint16_t start = pos;
        while ((pos < length) && (g_cloud.log_snapshot[pos] != '\n')) pos++;
        while ((pos > start) && ((g_cloud.log_snapshot[pos - 1U] == '\r') || (g_cloud.log_snapshot[pos - 1U] == '\n'))) pos--;
        if (pos > start)
        {
            g_cloud.log_line_offset[g_cloud.log_line_count] = start;
            g_cloud.log_line_length[g_cloud.log_line_count] = (uint16_t)(pos - start);
            g_cloud.log_line_count++;
        }
        while ((pos < length) && ((g_cloud.log_snapshot[pos] == '\r') || (g_cloud.log_snapshot[pos] == '\n'))) pos++;
    }

    g_cloud.log_chunk_total = 0U;
    for (i = 0U; i < g_cloud.log_line_count; i++)
    {
        uint16_t item_size = (uint16_t)(escaped_length(g_cloud.log_snapshot + g_cloud.log_line_offset[i], g_cloud.log_line_length[i]) + 1U);
        if ((g_cloud.log_chunk_total == 0U) || (g_cloud.log_chunk_count[chunk] >= CLOUD_LOG_LINES_PER_PACKET) ||
            ((uint32_t)estimated + item_size >= CLOUD_PAYLOAD_LIMIT))
        {
            chunk = g_cloud.log_chunk_total++;
            g_cloud.log_chunk_start[chunk] = i;
            g_cloud.log_chunk_count[chunk] = 0U;
            estimated = 220U;
        }
        g_cloud.log_chunk_count[chunk]++;
        estimated = (uint16_t)(estimated + item_size);
    }
    g_cloud.log_chunk_index = 0U;
    g_cloud.log_active = (g_cloud.log_chunk_total > 0U) ? 1U : 0U;
}

uint8_t CloudMessages_BuildLogChunk(char* output, uint16_t size)
{
    JsonWriter_t writer;
    uint8_t i;
    uint8_t start = g_cloud.log_chunk_start[g_cloud.log_chunk_index];
    uint8_t count = g_cloud.log_chunk_count[g_cloud.log_chunk_index];
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"2.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_append(&writer, ",\"data\":{\"product\":\"patrol_vehicle_chassis\",\"requestId\":");
    json_string(&writer, g_cloud.log_request_id);
    json_appendf(&writer, ",\"sequence\":%u,\"total\":%u,\"lines\":[",
                 (unsigned int)(g_cloud.log_chunk_index + 1U), (unsigned int)g_cloud.log_chunk_total);
    for (i = 0U; i < count; i++)
    {
        uint8_t line = (uint8_t)(start + i);
        if (i != 0U) json_append(&writer, ",");
        json_string_n(&writer, g_cloud.log_snapshot + g_cloud.log_line_offset[line], g_cloud.log_line_length[line]);
    }
    json_append(&writer, "]}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

uint8_t CloudMessages_BuildAlert(char* output, uint16_t size, const char* line, AppLogLevel_t level)
{
    JsonWriter_t writer;
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"2.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_appendf(&writer, ",\"data\":{\"product\":\"patrol_vehicle_chassis\",\"level\":\"%s\",\"lines\":[",
                 level == APP_LOG_LEVEL_ERROR ? "ERROR" : "WARN");
    json_string(&writer, line);
    json_append(&writer, "]}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

void CloudMessages_BuildCalibrationResult(char* output, uint16_t size)
{
    const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
    if (output == 0 || size == 0U || cfg == 0) return;
    (void)snprintf(output, size,
        "{\"profileCompatible\":%s,\"configVersion\":%u,\"calibrated\":%s,\"remoteEnabled\":%s,\"steeringCenterCount\":%ld,"
        "\"absoluteCenterCount\":%lu,\"steeringCountsPerDegreeMilli\":%lu,"
        "\"absoluteCountsPerDegreeMilli\":%lu,\"steeringDirection\":%d,"
        "\"absoluteDirection\":%d,\"steeringToleranceMdeg\":%u}",
        PatrolConfig_CalibrationCompatible() ? "true" : "false",
        (unsigned int)cfg->version, cfg->calibrated ? "true" : "false",
        cfg->remote_enabled ? "true" : "false",
        (long)cfg->steering_center_count, (unsigned long)cfg->absolute_center_count,
        (unsigned long)cfg->steering_counts_per_degree_milli,
        (unsigned long)cfg->absolute_counts_per_degree_milli,
        (int)cfg->steering_direction, (int)cfg->absolute_direction,
        (unsigned int)cfg->steering_tolerance_mdeg);
}
