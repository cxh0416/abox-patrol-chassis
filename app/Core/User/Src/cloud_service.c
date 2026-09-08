#include "cloud_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_log.h"
#include "app_version.h"
#include "abox_boot_v2.h"
#include "abox_boot_v2_app.h"
#include "boot_state.h"
#include "boot_version.h"
#include "cloud_protocol.h"
#include "ec800_at.h"
#include "abox_ec800_rx.h"
#include "ec_power.h"
#include "fixed_point.h"
#include "mqtt_core.h"
#include "ota_service.h"
#include "remote_service.h"
#include "time_base.h"
#include "usart.h"
#include "vcu_config.h"
#include "vcu_rtos.h"
#include "vehicle_power_output.h"

#define CLOUD_RX_DMA_SIZE (EC800_AT_RAW_CHUNK_MAX + 64U)
#define CLOUD_RX_RING_SIZE 4096U
#define CLOUD_PAYLOAD_SIZE 1100U
#define CLOUD_PAYLOAD_LIMIT 1080U
#define CLOUD_TOPIC_SIZE 128U
#define CLOUD_REQUEST_ID_SIZE CLOUD_PROTOCOL_REQUEST_ID_SIZE
#define CLOUD_HEARTBEAT_MS 5000U
#define CLOUD_STATUS_REPORT_MS 1000U
#define CLOUD_REBOOT_DELAY_MS 300U
#define CLOUD_LOG_SNAPSHOT_SIZE 3584U
#define CLOUD_ALERT_PAYLOAD_SIZE 512U
#define CLOUD_LOG_MAX_LINES 32U
#define CLOUD_LOG_LINES_PER_PACKET 6U

typedef enum {
    CLOUD_TX_NONE = 0,
    CLOUD_TX_HEARTBEAT,
    CLOUD_TX_STATUS_REPORT,
    CLOUD_TX_RESPONSE,
    CLOUD_TX_LOG_CHUNK,
    CLOUD_TX_ALERT,
} CloudTxKind_t;

typedef struct {
    char* buffer;
    uint16_t capacity;
    uint16_t length;
    uint8_t valid;
} JsonWriter_t;

typedef struct {
    uint8_t valid;
    uint8_t safety_ready;
    uint8_t fault_latched;
    uint8_t reversal_pending;
    uint8_t power_output_on;
    uint8_t stop_reason;
    int16_t target_rpm;
    int32_t target_steering_mdeg;
} ChassisStatusSnapshot_t;

static Ec800AtCore_t g_at_core;
static uint8_t g_uart_dma[CLOUD_RX_DMA_SIZE];
static uint8_t g_rx_ring[CLOUD_RX_RING_SIZE];
static ABoxEc800Rx g_ec800_rx;

static char g_topic_heartbeat[CLOUD_TOPIC_SIZE];
static char g_topic_status_report[CLOUD_TOPIC_SIZE];
static char g_topic_config_cmd[CLOUD_TOPIC_SIZE];
static char g_topic_config_resp[CLOUD_TOPIC_SIZE];
static char g_topic_ota_cmd[CLOUD_TOPIC_SIZE];
static char g_topic_ota_resp[CLOUD_TOPIC_SIZE];
static char g_topic_log[CLOUD_TOPIC_SIZE];
static char g_topic_control_cmd[CLOUD_TOPIC_SIZE];
static char g_topic_control_resp[CLOUD_TOPIC_SIZE];

static uint8_t g_initialized;
static uint8_t g_config_ready;
static uint8_t g_mqtt_was_ready;
static uint8_t g_reset_reason;
static uint8_t g_ota_response_waiting;
static uint8_t g_ota_begin_pending;
static char g_ota_request_id[CLOUD_REQUEST_ID_SIZE];
static uint32_t g_heartbeat_due;
static uint32_t g_status_report_due;
static uint32_t g_status_report_sequence;
static ChassisStatusSnapshot_t g_status_snapshot;
static uint32_t g_iccid_due;
static uint32_t g_ota_begin_due;

static CloudTxKind_t g_tx_kind;
static uint32_t g_tx_start_sequence;
static char g_tx_payload[CLOUD_PAYLOAD_SIZE];

static uint8_t g_response_pending;
static uint8_t g_response_reboots;
static uint8_t g_response_ack_rollback;
static char g_response_request_id[CLOUD_REQUEST_ID_SIZE];
static char g_response_topic[CLOUD_TOPIC_SIZE];
static char g_response_payload[CLOUD_PAYLOAD_SIZE];

static char g_last_request_id[CLOUD_REQUEST_ID_SIZE];
static char g_last_response_topic[CLOUD_TOPIC_SIZE];
static char g_last_response_payload[CLOUD_PAYLOAD_SIZE];

static char g_log_request_id[CLOUD_REQUEST_ID_SIZE];
static char g_log_snapshot[CLOUD_LOG_SNAPSHOT_SIZE];
static uint16_t g_log_line_offset[CLOUD_LOG_MAX_LINES];
static uint16_t g_log_line_length[CLOUD_LOG_MAX_LINES];
static uint8_t g_log_chunk_start[CLOUD_LOG_MAX_LINES];
static uint8_t g_log_chunk_count[CLOUD_LOG_MAX_LINES];
static uint8_t g_log_line_count;
static uint8_t g_log_chunk_total;
static uint8_t g_log_chunk_index;
static uint8_t g_log_active;

static uint8_t g_alert_pending;
static char g_alert_payload[CLOUD_ALERT_PAYLOAD_SIZE];
static uint8_t g_reboot_pending;
static uint32_t g_reboot_due;

static void copy_string(char* dst, uint16_t size, const char* src)
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

static uint8_t read_reset_reason(void)
{
    uint8_t reason = 0U;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) reason = 3U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) reason = 4U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) reason = 5U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) reason = 6U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) reason = 2U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) reason = 1U;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return reason;
}

static void uart_start_rx(void)
{
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_uart_dma, sizeof(g_uart_dma));
}

static uint32_t ec800_tick(void *context)
{
    (void)context;
    return HAL_GetTick();
}

static int ec800_write(void *context, const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms)
{
    return HAL_UART_Transmit((UART_HandleTypeDef *)context, (uint8_t *)data,
                             length, timeout_ms) == HAL_OK;
}

static void ec800_rx_sink(void *context, const uint8_t *data, uint16_t length)
{
    EC800_AT_FeedRx((Ec800AtCore_t *)context, data, length);
}

static uint8_t start_publish(CloudTxKind_t kind, const char* topic, const char* payload,
                             uint8_t qos, uint8_t retain)
{
    if ((g_tx_kind != CLOUD_TX_NONE) || !MQTT_Core_IsReady() || MQTT_Core_IsPublishActive()) return 0U;
    if (!MQTT_Core_PublishEx(topic, payload, qos, retain)) return 0U;
    g_tx_kind = kind;
    g_tx_start_sequence = MQTT_Core_GetPubOkSeq();
    return 1U;
}

static void cache_response(const char* request_id, const char* topic, const char* payload)
{
    copy_string(g_last_request_id, sizeof(g_last_request_id), request_id);
    copy_string(g_last_response_topic, sizeof(g_last_response_topic), topic);
    copy_string(g_last_response_payload, sizeof(g_last_response_payload), payload);
}

static uint8_t schedule_response(const char* topic, const char* request_id, uint16_t code, const char* message,
                                 const char* result_json, uint8_t reboot)
{
    JsonWriter_t writer;
    if (g_response_pending) return 0U;
    json_init(&writer, g_response_payload, sizeof(g_response_payload));
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
    copy_string(g_response_topic, sizeof(g_response_topic), topic);
    copy_string(g_response_request_id, sizeof(g_response_request_id), request_id);
    g_response_pending = 1U;
    g_response_reboots = reboot;
    return 1U;
}

static void schedule_simple_error(const char* topic, const char* request_id, uint16_t code, const char* message)
{
    if (!schedule_response(topic, request_id, code, message, "{}", 0U))
        APP_LOGW("CLOUD", "response queue busy code=%u", (unsigned int)code);
}

static uint8_t schedule_raw_response(const char* topic, const char* payload)
{
    if (g_response_pending || topic == 0 || payload == 0 || strlen(payload) >= sizeof(g_response_payload)) return 0U;
    copy_string(g_response_topic, sizeof(g_response_topic), topic);
    copy_string(g_response_payload, sizeof(g_response_payload), payload);
    g_response_request_id[0] = '\0';
    g_response_pending = 1U;
    g_response_reboots = 0U;
    return 1U;
}

static void build_info_result(char* output, uint16_t size)
{
    JsonWriter_t writer;
    BootStateRecord_t state;
    ABoxBootV2Descriptor descriptor;
    uint8_t have_state = BootState_Load(&state);
    uint8_t descriptor_valid = (uint8_t)(ABoxBootV2_DescriptorRead(&descriptor) &&
                                         ABoxBootV2_DescriptorValid(&descriptor));
    const char* iccid = MQTT_Core_GetIccid();
    PatrolDiagnosticsSnapshot diagnostics;
    VcuRtos_GetDiagnostics(&diagnostics);
    json_init(&writer, output, size);
    json_append(&writer, "{\"fwVersion\":"); json_string(&writer, Vcu_AppFwVersion());
    json_append(&writer, ",\"bootVersion\":"); json_string(&writer, descriptor_valid ? descriptor.build_version : "");
    json_appendf(&writer, ",\"bootDescriptorValid\":%s,\"bootApiVersion\":%u,\"bootFeatures\":%lu",
                 descriptor_valid ? "true" : "false", descriptor_valid ? descriptor.abi_version : 0U,
                 (unsigned long)(descriptor_valid ? descriptor.feature_flags : 0U));
    json_append(&writer, ",\"otaScheme\":\"boot_v2_ufs\",\"otaDownloader\":\"app\"");
    json_append(&writer, ",\"stableSeedSource\":");
    json_string(&writer, ABoxBootV2App_StableSeedSource());
    json_append(&writer, ",\"otaBootstrapNeedsRevision\":false");
    json_append(&writer, ",\"vid\":"); json_string(&writer, VcuConfig_Get()->vid);
    json_appendf(&writer, ",\"uptimeMs\":%lu,\"mqttState\":", (unsigned long)HAL_GetTick());
    json_string(&writer, mqtt_state_name(MQTT_Core_GetState()));
    json_appendf(&writer, ",\"mqttStateCode\":%u,\"iccid\":", (unsigned int)MQTT_Core_GetState());
    json_string(&writer, iccid ? iccid : "");
    json_appendf(&writer,
                 ",\"canInitialized\":%s,\"canBusOff\":%s,\"canRxOverflow\":%lu,\"otaState\":",
                 VcuRtos_CanStarted() ? "true" : "false", VcuRtos_CanBusOff() ? "true" : "false",
                 (unsigned long)VcuRtos_CanRxOverflowCount());
    json_string(&writer, (have_state && state.rollback_report_pending) ? "ROLLBACK" :
                boot_state_name(have_state ? state.state : BOOT_STATE_NORMAL));
    json_appendf(&writer, ",\"powerOutputAvailable\":%s,\"powerOutputOn\":%s",
                 VehiclePowerOutput_IsAvailable() ? "true" : "false",
                 VehiclePowerOutput_IsOn() ? "true" : "false");
    json_appendf(&writer, ",\"stableSlot\":%u,\"candidateSlot\":%u,\"trialFailCount\":%u,"
                 "\"resetReason\":%u,\"otaProvisioned\":%s,\"otaReady\":%s,\"otaBusy\":%s,\"otaPhase\":",
                 have_state ? state.stable_slot : 0U, have_state ? state.candidate_slot : 0U,
                 have_state ? state.trial_fail_count : 0U, (unsigned int)g_reset_reason,
                 OtaService_IsProvisioned() ? "true" : "false",
                 OtaService_IsReady() ? "true" : "false", OtaService_IsBusy() ? "true" : "false");
    json_string(&writer, OtaService_Phase());
    json_append(&writer, ",\"otaTargetVersion\":");
    json_string(&writer, OtaService_IsBusy() ? OtaService_TargetVersion() : (have_state ? state.image_version : ""));
    json_appendf(&writer, ",\"lastOtaError\":%lu,\"diagnostics\":{\"canRx\":%lu,"
                 "\"steerHb\":%s,\"driveHb\":%s,\"steerRaw\":%ld,\"leftRpm\":%ld,"
                 "\"rightRpm\":%ld,\"encoderRaw\":%lu,\"encoderOnline\":%s,"
                 "\"sdoTimeouts\":%lu,\"encoderTimeouts\":%lu}}",
                 (unsigned long)(OtaService_LastError() ? OtaService_LastError() :
                                 (have_state ? state.last_error : 0U)),
                 (unsigned long)diagnostics.can_rx_count,
                 diagnostics.steering_online ? "true" : "false",
                 diagnostics.drive_online ? "true" : "false",
                 (long)diagnostics.steering_position_count,
                 (long)diagnostics.left_actual_rpm, (long)diagnostics.right_actual_rpm,
                 (unsigned long)diagnostics.encoder_count,
                 diagnostics.encoder_online ? "true" : "false",
                 (unsigned long)diagnostics.sdo_timeout_count,
                 (unsigned long)diagnostics.encoder_timeout_count);
    if (!writer.valid) copy_string(output, size, "{}");
}

static uint8_t build_heartbeat(char* output, uint16_t size)
{
    JsonWriter_t writer;
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"3.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_append(&writer, ",\"data\":{\"deviceType\":\"vehicle_chassis\"");
    json_append(&writer, ",\"firmware\":"); json_string(&writer, Vcu_AppFwVersion());
    json_append(&writer, "}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

static void remember_status_snapshot(uint32_t now)
{
    const ChassisControl* control = RemoteService_State();
    g_status_snapshot.valid = 1U;
    g_status_snapshot.safety_ready = ChassisControl_SafetyReady(control, now);
    g_status_snapshot.fault_latched = control->fault_latched;
    g_status_snapshot.reversal_pending = control->reversal_pending;
    g_status_snapshot.power_output_on = VehiclePowerOutput_IsOn();
    g_status_snapshot.stop_reason = (uint8_t)control->stop_reason;
    g_status_snapshot.target_rpm = control->target_rpm;
    g_status_snapshot.target_steering_mdeg = control->target_steering_mdeg;
}

static const char* status_change_reason(uint32_t now)
{
    const ChassisControl* control = RemoteService_State();
    uint8_t safety_ready;
    if (!g_status_snapshot.valid) return 0;
    safety_ready = ChassisControl_SafetyReady(control, now);
    if (control->fault_latched != g_status_snapshot.fault_latched)
        return control->fault_latched ? "fault_raised" : "fault_cleared";
    if ((g_status_snapshot.safety_ready && !safety_ready) ||
        (g_status_snapshot.target_rpm != 0 && control->target_rpm == 0 &&
         control->stop_reason != CHASSIS_STOP_NONE))
        return "safety_stop";
    if (safety_ready != g_status_snapshot.safety_ready ||
        VehiclePowerOutput_IsOn() != g_status_snapshot.power_output_on ||
        control->reversal_pending != g_status_snapshot.reversal_pending ||
        control->stop_reason != (ChassisStopReason)g_status_snapshot.stop_reason ||
        control->target_rpm != g_status_snapshot.target_rpm ||
        control->target_steering_mdeg != g_status_snapshot.target_steering_mdeg)
        return "state_changed";
    return 0;
}

static uint8_t build_status_report(char* output, uint16_t size, const char* report_reason)
{
    JsonWriter_t writer;
    const ChassisControl* control = RemoteService_State();
    const ChassisFeedback* feedback = &control->feedback;
    uint32_t now = HAL_GetTick();
    uint8_t feedback_fresh = (uint8_t)((now - feedback->feedback_ms) <= CHASSIS_FEEDBACK_TIMEOUT_MS);
    const char* motion_state = "stopped";
    char timestamp[21];
    char report_id[96];
    char target_steering[24];
    char actual_steering[24];
    char actual_encoder[24];
    PatrolDiagnosticsSnapshot diagnostics;
    MQTT_CoreRxDiagnostics_t mqtt_rx;
    if (control->fault_latched) motion_state = "fault";
    else if (control->reversal_pending) motion_state = "reversing_wait";
    else if (feedback_fresh &&
             (abs(feedback->left_actual_rpm) > 20 || abs(feedback->right_actual_rpm) > 20))
        motion_state = "driving";
    if (!TimeBase_FormatUtcMs(timestamp, sizeof(timestamp))) strcpy(timestamp, "0");
    (void)snprintf(report_id, sizeof(report_id), "%s-%s-%lu", VcuConfig_Get()->vid,
                   timestamp, (unsigned long)g_status_report_sequence++);
    if (!FixedPoint_FormatMilli(target_steering, sizeof(target_steering),
                                control->target_steering_mdeg) ||
        !FixedPoint_FormatMilli(actual_steering, sizeof(actual_steering),
                                feedback->steering_actual_mdeg) ||
        !FixedPoint_FormatMilli(actual_encoder, sizeof(actual_encoder),
                                feedback->encoder_actual_mdeg)) return 0U;
    VcuRtos_GetDiagnostics(&diagnostics);
    MQTT_Core_GetRxDiagnostics(&mqtt_rx);
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"3.0\",\"timestamp\":");
    json_append(&writer, timestamp);
    json_append(&writer, ",\"data\":{\"deviceType\":\"vehicle_chassis\",\"reportId\":");
    json_string(&writer, report_id);
    json_append(&writer, ",\"reportReason\":");
    json_string(&writer, report_reason ? report_reason : "periodic");
    json_append(&writer, ",\"motionState\":");
    json_string(&writer, motion_state);
    json_appendf(&writer,
        ",\"targetDriveRpm\":%d,\"targetSteeringDeg\":%s,\"leftActualRpm\":%d,"
        "\"rightActualRpm\":%d,\"steeringActualDeg\":%s,\"absoluteActualDeg\":%s,"
        "\"steeringOnline\":%s,\"driveOnline\":%s,\"encoderOnline\":%s,"
        "\"canBusOff\":%s,\"driveFault\":%s,\"feedbackFresh\":%s,"
        "\"calibrated\":%s,\"powerReady\":%s,\"powerOutputOn\":%s,\"otaBusy\":%s,\"safetyReady\":%s,"
        "\"stopReason\":%u,\"faultLatched\":%s,"
        "\"transportDiagnostics\":{\"mqttRxLines\":%lu,\"mqttRxDecoded\":%lu,"
        "\"mqttRxRejected\":%lu,\"mqttRxDispatched\":%lu,\"mqttRxOverwrites\":%lu,"
        "\"mqttRxTimeouts\":%lu,\"mqttRxLastMs\":%lu,\"ecDmaOverflows\":%lu,"
        "\"ecDmaRestarts\":%lu,\"ecDmaMaxBacklog\":%lu,\"ecLineOverflows\":%lu}}}",
        (int)control->target_rpm, target_steering,
        (int)feedback->left_actual_rpm, (int)feedback->right_actual_rpm,
        actual_steering, actual_encoder,
        feedback->steering_online ? "true" : "false",
        feedback->drive_online ? "true" : "false",
        diagnostics.encoder_online ? "true" : "false",
        feedback->can_bus_off ? "true" : "false",
        feedback->drive_fault ? "true" : "false", feedback_fresh ? "true" : "false",
        feedback->calibrated ? "true" : "false", feedback->power_ready ? "true" : "false",
        VehiclePowerOutput_IsOn() ? "true" : "false",
        feedback->ota_busy ? "true" : "false",
        ChassisControl_SafetyReady(control, now) ? "true" : "false",
        (unsigned int)control->stop_reason, control->fault_latched ? "true" : "false",
        (unsigned long)mqtt_rx.qmtrecv_line_count,
        (unsigned long)mqtt_rx.decoded_message_count,
        (unsigned long)mqtt_rx.rejected_frame_count,
        (unsigned long)mqtt_rx.dispatched_message_count,
        (unsigned long)mqtt_rx.pending_overwrite_count,
        (unsigned long)mqtt_rx.multiline_timeout_count,
        (unsigned long)mqtt_rx.last_qmtrecv_tick,
        (unsigned long)ABoxEc800Rx_OverflowCount(&g_ec800_rx),
        (unsigned long)ABoxEc800Rx_RestartCount(&g_ec800_rx),
        (unsigned long)ABoxEc800Rx_MaxBacklog(&g_ec800_rx),
        (unsigned long)EC800_AT_LineOverflowCount(&g_at_core));
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

static void prepare_log_snapshot(const char* request_id)
{
    uint16_t length;
    uint16_t pos = 0U;
    uint8_t i;
    uint8_t chunk = 0U;
    uint16_t estimated = 220U;

    copy_string(g_log_request_id, sizeof(g_log_request_id), request_id);
    length = AppLog_Dump(g_log_snapshot, sizeof(g_log_snapshot));
    g_log_line_count = 0U;
    while ((pos < length) && (g_log_line_count < CLOUD_LOG_MAX_LINES))
    {
        uint16_t start = pos;
        while ((pos < length) && (g_log_snapshot[pos] != '\n')) pos++;
        while ((pos > start) && ((g_log_snapshot[pos - 1U] == '\r') || (g_log_snapshot[pos - 1U] == '\n'))) pos--;
        if (pos > start)
        {
            g_log_line_offset[g_log_line_count] = start;
            g_log_line_length[g_log_line_count] = (uint16_t)(pos - start);
            g_log_line_count++;
        }
        while ((pos < length) && ((g_log_snapshot[pos] == '\r') || (g_log_snapshot[pos] == '\n'))) pos++;
    }

    g_log_chunk_total = 0U;
    for (i = 0U; i < g_log_line_count; i++)
    {
        uint16_t item_size = (uint16_t)(escaped_length(g_log_snapshot + g_log_line_offset[i], g_log_line_length[i]) + 1U);
        if ((g_log_chunk_total == 0U) || (g_log_chunk_count[chunk] >= CLOUD_LOG_LINES_PER_PACKET) ||
            ((uint32_t)estimated + item_size >= CLOUD_PAYLOAD_LIMIT))
        {
            chunk = g_log_chunk_total++;
            g_log_chunk_start[chunk] = i;
            g_log_chunk_count[chunk] = 0U;
            estimated = 220U;
        }
        g_log_chunk_count[chunk]++;
        estimated = (uint16_t)(estimated + item_size);
    }
    g_log_chunk_index = 0U;
    g_log_active = (g_log_chunk_total > 0U) ? 1U : 0U;
}

static uint8_t build_log_chunk(char* output, uint16_t size)
{
    JsonWriter_t writer;
    uint8_t i;
    uint8_t start = g_log_chunk_start[g_log_chunk_index];
    uint8_t count = g_log_chunk_count[g_log_chunk_index];
    json_init(&writer, output, size);
    json_append(&writer, "{\"version\":\"2.0\",\"timestamp\":");
    json_append_timestamp(&writer);
    json_append(&writer, ",\"data\":{\"product\":\"patrol_vehicle_chassis\",\"requestId\":");
    json_string(&writer, g_log_request_id);
    json_appendf(&writer, ",\"sequence\":%u,\"total\":%u,\"lines\":[",
                 (unsigned int)(g_log_chunk_index + 1U), (unsigned int)g_log_chunk_total);
    for (i = 0U; i < count; i++)
    {
        uint8_t line = (uint8_t)(start + i);
        if (i != 0U) json_append(&writer, ",");
        json_string_n(&writer, g_log_snapshot + g_log_line_offset[line], g_log_line_length[line]);
    }
    json_append(&writer, "]}}");
    return (uint8_t)(writer.valid && writer.length < CLOUD_PAYLOAD_LIMIT);
}

static uint8_t build_alert(char* output, uint16_t size, const char* line, AppLogLevel_t level)
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

static uint8_t boot_state_allows_reboot(void)
{
    BootStateRecord_t state;
    if (!BootState_Load(&state)) return 1U;
    return (state.state == BOOT_STATE_NORMAL || state.state == BOOT_STATE_CONFIRMED) ? 1U : 0U;
}

static void build_calibration_result(char* output, uint16_t size)
{
    const VcuRemoteConfig_t* cfg = VcuConfig_Get();
    if (output == 0 || size == 0U || cfg == 0) return;
    (void)snprintf(output, size,
        "{\"profileCompatible\":%s,\"configVersion\":%u,\"calibrated\":%s,\"remoteEnabled\":%s,\"steeringCenterCount\":%ld,"
        "\"absoluteCenterCount\":%lu,\"steeringCountsPerDegreeMilli\":%lu,"
        "\"absoluteCountsPerDegreeMilli\":%lu,\"steeringDirection\":%d,"
        "\"absoluteDirection\":%d,\"steeringToleranceMdeg\":%u}",
        VcuConfig_CalibrationCompatible() ? "true" : "false",
        (unsigned int)cfg->version, cfg->calibrated ? "true" : "false",
        cfg->remote_enabled ? "true" : "false",
        (long)cfg->steering_center_count, (unsigned long)cfg->absolute_center_count,
        (unsigned long)cfg->steering_counts_per_degree_milli,
        (unsigned long)cfg->absolute_counts_per_degree_milli,
        (int)cfg->steering_direction, (int)cfg->absolute_direction,
        (unsigned int)cfg->steering_tolerance_mdeg);
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
    if (!VehiclePowerOutput_IsOn() || VcuRtos_CanBusOff() || VcuRtos_CanBusOffActive() ||
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

static void handle_config_request(const CloudProtocolRequest_t* request)
{
    char result[900];
    CloudProtocolAction_t action;
    const char* request_id;
    if (request == 0) return;
    action = request->action;
    request_id = request->request_id;
    if (action == CLOUD_PROTOCOL_ACTION_GET_INFO)
    {
        build_info_result(result, sizeof(result));
        if (schedule_response(g_topic_config_resp, request_id, 200U, "OK", result, 0U))
            g_response_ack_rollback = 1U;
        else
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_CAN_DIAG)
    {
        PatrolDiagnosticsSnapshot snapshot;
        VcuRtos_GetDiagnostics(&snapshot);
        if (!PatrolDiagnostics_FormatCanJson(&snapshot, VcuRtos_CanBusOff(),
                VcuRtos_CanBusOffActive(), VcuRtos_CanRxOverflowCount(),
                VcuRtos_CanRawRxCount(0U), VcuRtos_CanRawRxCount(1U), result, sizeof(result)))
            schedule_simple_error(g_topic_config_resp, request_id, 500U, "DIAGNOSTIC_TOO_LARGE");
        else if (!schedule_response(g_topic_config_resp, request_id, 200U, "OK", result, 0U))
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_CALIBRATION)
    {
        build_calibration_result(result, sizeof(result));
        if (!schedule_response(g_topic_config_resp, request_id, 200U, "OK", result, 0U))
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "DEVICE_BUSY");
    }
    else if (action == CLOUD_PROTOCOL_ACTION_CLEAR_CALIBRATION)
    {
        if (OtaService_IsBusy())
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "OTA_BUSY");
        else if (RemoteService_BenchPermitted())
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "BENCH_ACTIVE");
        else if (!VcuConfig_ClearCalibration())
            schedule_simple_error(g_topic_config_resp, request_id, 500U, "CONFIG_WRITE_FAILED");
        else
        {
            build_calibration_result(result, sizeof(result));
            (void)schedule_response(g_topic_config_resp, request_id, 200U,
                                    "CALIBRATION_CLEARED", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_SET_CALIBRATION)
    {
        PatrolDiagnosticsSnapshot snapshot;
        VcuRtos_GetDiagnostics(&snapshot);
        if (OtaService_IsBusy())
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "OTA_BUSY");
        else if (RemoteService_BenchPermitted() || !calibration_feedback_ready(request, &snapshot))
            schedule_simple_error(g_topic_config_resp, request_id, 409U,
                                  "CALIBRATION_FEEDBACK_NOT_READY");
        else if (!VcuConfig_SetCalibration(request->steering_center_count,
                    request->absolute_center_count, request->steering_counts_per_degree_milli,
                    request->absolute_counts_per_degree_milli, request->steering_direction,
                    request->absolute_direction, request->steering_tolerance_mdeg))
            schedule_simple_error(g_topic_config_resp, request_id, 500U, "CONFIG_WRITE_FAILED");
        else
        {
            build_calibration_result(result, sizeof(result));
            (void)schedule_response(g_topic_config_resp, request_id, 200U,
                                    "CALIBRATION_SAVED_REMOTE_DISABLED", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_BENCH_ARM || action == CLOUD_PROTOCOL_ACTION_BENCH_STOP ||
             action == CLOUD_PROTOCOL_ACTION_BENCH_GET)
    {
        if (action == CLOUD_PROTOCOL_ACTION_BENCH_ARM && !VcuRtos_BenchArm(request->bench_limit_mdeg))
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "BENCH_NOT_READY");
        else {
            if (action == CLOUD_PROTOCOL_ACTION_BENCH_STOP) VcuRtos_BenchStop();
            VcuRtos_FormatBench(result, sizeof(result));
            (void)schedule_response(g_topic_config_resp, request_id, 200U, "OK", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_GET_LOG)
    {
        if (g_log_active)
        {
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "DEVICE_BUSY");
            return;
        }
        prepare_log_snapshot(request_id);
        if (!g_log_active)
        {
            (void)snprintf(result, sizeof(result), "{\"lineCount\":0,\"dropped\":%lu}",
                           (unsigned long)AppLog_GetMqttDroppedCount());
            (void)schedule_response(g_topic_config_resp, request_id, 200U, "OK", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_REBOOT)
    {
        if (!boot_state_allows_reboot())
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "DEVICE_BUSY");
        else
            (void)schedule_response(g_topic_config_resp, request_id, 200U, "REBOOTING", "{}", 1U);
    }
    else if (action == CLOUD_PROTOCOL_ACTION_POWER_ON || action == CLOUD_PROTOCOL_ACTION_POWER_OFF)
    {
        uint8_t target_on = action == CLOUD_PROTOCOL_ACTION_POWER_ON ? 1U : 0U;
        if (!VehiclePowerOutput_IsAvailable())
            schedule_simple_error(g_topic_config_resp, request_id, 503U, "POWER_OUTPUT_UNAVAILABLE");
        else if (!VehiclePowerOutput_Set(target_on))
            schedule_simple_error(g_topic_config_resp, request_id, 500U, "POWER_OUTPUT_WRITE_FAILED");
        else
        {
            (void)snprintf(result, sizeof(result), "{\"powerOutputOn\":%s}",
                           target_on ? "true" : "false");
            (void)schedule_response(g_topic_config_resp, request_id, 200U,
                                    target_on ? "POWER_ON" : "POWER_OFF", result, 0U);
        }
    }
    else if (action == CLOUD_PROTOCOL_ACTION_SET_MQTT)
    {
        /* Reply on the old VID topic before reset; the next heartbeat carries the new identity. */
        if (!boot_state_allows_reboot()) {
            schedule_simple_error(g_topic_config_resp, request_id, 409U, "DEVICE_BUSY");
        } else if (!VcuConfig_SetMqtt(request->mqtt_vid, request->mqtt_host, request->mqtt_port,
                                      request->mqtt_username, request->mqtt_password)) {
            schedule_simple_error(g_topic_config_resp, request_id, 500U, "CONFIG_WRITE_FAILED");
        } else {
            (void)snprintf(result, sizeof(result), "{\"vid\":\"%s\",\"rebooting\":true}", request->mqtt_vid);
            (void)schedule_response(g_topic_config_resp, request_id, 200U, "MQTT_CONFIG_SAVED", result, 1U);
        }
    }
}

static void handle_ota_request(const CloudProtocolRequest_t* request)
{
    BootStateRecord_t state;
    if (request == 0) return;
    /* Same requestId is idempotent while the QoS1 response is still in flight. */
    if (g_ota_response_waiting) {
        if (strcmp(request->request_id, g_ota_request_id) == 0) return;
        schedule_simple_error(g_topic_ota_resp, request->request_id, 409U, "DEVICE_BUSY");
        return;
    }
    if (BootState_Load(&state) && state.state == BOOT_STATE_CONFIRMED &&
        strcmp(request->ota_version, Vcu_AppFwVersion()) == 0) {
        (void)schedule_response(g_topic_ota_resp, request->request_id, 200U, "ALREADY_CONFIRMED",
                                "{\"otaPhase\":\"CONFIRMED\"}", 0U);
        return;
    }
    if (OtaService_IsBusy()) { schedule_simple_error(g_topic_ota_resp, request->request_id, 409U, "DEVICE_BUSY"); return; }
    if (BootState_Load(&state) && (state.state != BOOT_STATE_NORMAL && state.state != BOOT_STATE_CONFIRMED)) {
        schedule_simple_error(g_topic_ota_resp, request->request_id, 409U, "DEVICE_BUSY"); return;
    }
    if (!OtaService_IsReady()) { schedule_simple_error(g_topic_ota_resp, request->request_id, 503U, "EC800_UNSUPPORTED"); return; }
    if (!OtaService_Start(request)) {
        uint32_t error = OtaService_LastError();
        uint16_t status = (error == OTA_ERROR_URL_REJECTED) ? 400U :
                          (error == OTA_ERROR_EC800_UNSUPPORTED || error == OTA_ERROR_CA_PROVISION_FAILED) ? 503U : 500U;
        char result[64];
        (void)snprintf(result, sizeof(result), "{\"errorCode\":%lu}", (unsigned long)error);
        (void)schedule_response(g_topic_ota_resp, request->request_id, status, "OTA_REQUEST_REJECTED", result, 0U);
        return;
    }
    copy_string(g_ota_request_id, sizeof(g_ota_request_id), request->request_id);
    g_ota_response_waiting = 1U;
}

static void cloud_on_message(const char* topic, const char* payload)
{
    CloudProtocolRequest_t request;
    CloudProtocolStatus_t status;
    uint16_t payload_length;
    const char* response_topic;
    uint8_t ota_topic;

    if ((topic == 0) || (payload == 0)) return;
    if (strcmp(topic, g_topic_control_cmd) == 0)
    {
        char response[CLOUD_PAYLOAD_SIZE];
        if (RemoteService_HandleControl(payload, (uint16_t)strlen(payload), response, sizeof(response)))
            (void)schedule_raw_response(g_topic_control_resp, response);
        else APP_LOGW("REMOTE", "invalid or busy chassis control request");
        return;
    }
    if ((strcmp(topic, g_topic_config_cmd) != 0) && (strcmp(topic, g_topic_ota_cmd) != 0)) return;
    ota_topic = (strcmp(topic, g_topic_ota_cmd) == 0) ? 1U : 0U;
    response_topic = ota_topic ? g_topic_ota_resp : g_topic_config_resp;
    payload_length = (uint16_t)strlen(payload);
    status = CloudProtocol_Parse(payload, payload_length, ota_topic, &request);
    if (status != CLOUD_PROTOCOL_OK)
    {
        if (request.request_id[0] != '\0')
            schedule_simple_error(response_topic, request.request_id, (uint16_t)status,
                                  CloudProtocol_StatusMessage(status));
        else APP_LOGW("CLOUD", "uncorrelated invalid request code=%u", (unsigned int)status);
        return;
    }
    if (strcmp(request.request_id, g_last_request_id) == 0 && g_last_response_payload[0] != '\0')
    {
        if (!g_response_pending)
        {
            copy_string(g_response_topic, sizeof(g_response_topic), g_last_response_topic);
            copy_string(g_response_payload, sizeof(g_response_payload), g_last_response_payload);
            copy_string(g_response_request_id, sizeof(g_response_request_id), request.request_id);
            g_response_pending = 1U;
            g_response_reboots = 0U;
        }
        return;
    }
    if (g_response_pending || g_reboot_pending)
    {
        APP_LOGW("CLOUD", "command busy");
        return;
    }
    if (g_log_active)
    {
        if (strcmp(request.request_id, g_log_request_id) != 0)
            schedule_simple_error(response_topic, request.request_id, 409U, "DEVICE_BUSY");
        return;
    }
    if (ota_topic) handle_ota_request(&request);
    else handle_config_request(&request);
}

static void tx_completed(void)
{
    char result[128];
    CloudTxKind_t completed = g_tx_kind;
    g_tx_kind = CLOUD_TX_NONE;
    if (completed == CLOUD_TX_RESPONSE)
    {
        cache_response(g_response_request_id, g_response_topic, g_response_payload);
        g_response_pending = 0U;
        if (g_response_reboots)
        {
            g_reboot_pending = 1U;
            g_reboot_due = HAL_GetTick() + CLOUD_REBOOT_DELAY_MS;
        }
        g_response_reboots = 0U;
        if (g_response_ack_rollback)
        {
            OtaService_AcknowledgeRollback();
            g_response_ack_rollback = 0U;
        }
    }
    else if (completed == CLOUD_TX_HEARTBEAT)
    {
    }
    else if (completed == CLOUD_TX_STATUS_REPORT)
    {
    }
    else if (completed == CLOUD_TX_LOG_CHUNK)
    {
        g_log_chunk_index++;
        if (g_log_chunk_index >= g_log_chunk_total)
        {
            g_log_active = 0U;
            (void)snprintf(result, sizeof(result), "{\"lineCount\":%u,\"dropped\":%lu}",
                           (unsigned int)g_log_line_count, (unsigned long)AppLog_GetMqttDroppedCount());
            (void)schedule_response(g_topic_config_resp, g_log_request_id, 200U, "OK", result, 0U);
        }
    }
    else if (completed == CLOUD_TX_ALERT) g_alert_pending = 0U;
}

static void service_tx(void)
{
    uint32_t now = HAL_GetTick();
    if (g_tx_kind != CLOUD_TX_NONE)
    {
        if (MQTT_Core_GetPubOkSeq() != g_tx_start_sequence) tx_completed();
        else if (!MQTT_Core_IsReady() && !MQTT_Core_IsPublishActive()) g_tx_kind = CLOUD_TX_NONE;
        else return;
    }
    if (!MQTT_Core_IsReady() || MQTT_Core_IsPublishActive()) return;

    if (g_response_pending)
    {
        (void)start_publish(CLOUD_TX_RESPONSE, g_response_topic, g_response_payload, 1U, 0U);
        return;
    }
    {
        const char* event_reason = status_change_reason(now);
        if (event_reason != 0)
        {
            if (build_status_report(g_tx_payload, sizeof(g_tx_payload), event_reason) &&
                start_publish(CLOUD_TX_STATUS_REPORT, g_topic_status_report, g_tx_payload, 1U, 1U))
                remember_status_snapshot(now);
            return;
        }
    }
    if ((int32_t)(now - g_heartbeat_due) >= 0)
    {
        if (build_heartbeat(g_tx_payload, sizeof(g_tx_payload)) &&
            start_publish(CLOUD_TX_HEARTBEAT, g_topic_heartbeat, g_tx_payload, 0U, 0U))
            g_heartbeat_due = now + CLOUD_HEARTBEAT_MS;
        return;
    }
    if ((int32_t)(now - g_status_report_due) >= 0)
    {
        if (build_status_report(g_tx_payload, sizeof(g_tx_payload), "periodic") &&
            start_publish(CLOUD_TX_STATUS_REPORT, g_topic_status_report, g_tx_payload, 0U, 1U))
        {
            g_status_report_due = now + CLOUD_STATUS_REPORT_MS;
            remember_status_snapshot(now);
        }
        return;
    }
    if (g_log_active)
    {
        if (!build_log_chunk(g_tx_payload, sizeof(g_tx_payload)))
        {
            g_log_active = 0U;
            schedule_simple_error(g_topic_config_resp, g_log_request_id, 500U, "LOG_ENCODING_FAILED");
            return;
        }
        (void)start_publish(CLOUD_TX_LOG_CHUNK, g_topic_log, g_tx_payload, 1U, 0U);
        return;
    }
    if (g_alert_pending)
    {
        (void)start_publish(CLOUD_TX_ALERT, g_topic_log, g_alert_payload, 0U, 0U);
        return;
    }
    {
        char line[160];
        AppLogLevel_t level;
        if (AppLog_ReadMqttAlert(line, sizeof(line), &level) > 0U && build_alert(g_alert_payload, sizeof(g_alert_payload), line, level))
        {
            g_alert_pending = 1U;
            (void)start_publish(CLOUD_TX_ALERT, g_topic_log, g_alert_payload, 0U, 0U);
            return;
        }
    }
    if (!MQTT_Core_IsIccidValid() && !MQTT_Core_IsIccidPending() && (int32_t)(now - g_iccid_due) >= 0)
    {
        MQTT_Core_RequestIccid();
        g_iccid_due = now + 30000U;
    }
}

void CloudService_Init(void)
{
    MQTT_CoreConfig_t mqtt;
    const VcuRemoteConfig_t* cfg = VcuConfig_Get();
    memset(&mqtt, 0, sizeof(mqtt));
    (void)ABoxEc800Rx_Init(&g_ec800_rx, g_uart_dma, sizeof(g_uart_dma),
                           g_rx_ring, sizeof(g_rx_ring));
    g_tx_kind = CLOUD_TX_NONE;
    g_response_pending = 0U;
    g_log_active = 0U;
    g_alert_pending = 0U;
    g_reboot_pending = 0U;
    g_ota_begin_pending = 0U;
    g_last_request_id[0] = '\0';
    g_last_response_payload[0] = '\0';
    g_reset_reason = read_reset_reason();
    g_config_ready = VcuConfig_IsCloudReady(cfg);

    {
        const ABoxEc800AtPort at_port = {&huart1, ec800_tick, ec800_write, 0};
        (void)EC800_AT_Init(&g_at_core, &at_port);
    }
    OtaService_Init(&g_at_core);
    mqtt.client_idx = 0U;
    if (cfg != 0)
    {
        copy_string(mqtt.server_host, sizeof(mqtt.server_host), cfg->mqtt_host);
        mqtt.server_port = cfg->mqtt_port;
        copy_string(mqtt.username, sizeof(mqtt.username), cfg->mqtt_username);
        copy_string(mqtt.password, sizeof(mqtt.password), cfg->mqtt_password);
        (void)snprintf(mqtt.client_id, sizeof(mqtt.client_id), "patrol_vehicle_chassis_%s", cfg->vid);
        (void)snprintf(g_topic_heartbeat, sizeof(g_topic_heartbeat), "/zxwl/abox/%s/heartbeat", cfg->vid);
        (void)snprintf(g_topic_status_report, sizeof(g_topic_status_report), "/zxwl/abox/%s/status/report", cfg->vid);
        (void)snprintf(g_topic_config_cmd, sizeof(g_topic_config_cmd), "/zxwl/abox/%s/internal/config", cfg->vid);
        (void)snprintf(g_topic_config_resp, sizeof(g_topic_config_resp), "/zxwl/abox/%s/internal/config/resp", cfg->vid);
        (void)snprintf(g_topic_ota_cmd, sizeof(g_topic_ota_cmd), "/zxwl/abox/%s/internal/ota", cfg->vid);
        (void)snprintf(g_topic_ota_resp, sizeof(g_topic_ota_resp), "/zxwl/abox/%s/internal/ota/resp", cfg->vid);
        (void)snprintf(g_topic_log, sizeof(g_topic_log), "/zxwl/abox/%s/internal/log", cfg->vid);
        (void)snprintf(g_topic_control_cmd, sizeof(g_topic_control_cmd), "/zxwl/abox/%s/ctrl", cfg->vid);
        (void)snprintf(g_topic_control_resp, sizeof(g_topic_control_resp), "/zxwl/abox/%s/ctrl/resp", cfg->vid);
        copy_string(mqtt.sub_topics[0], sizeof(mqtt.sub_topics[0]), g_topic_config_cmd);
        copy_string(mqtt.sub_topics[1], sizeof(mqtt.sub_topics[1]), g_topic_ota_cmd);
        copy_string(mqtt.sub_topics[2], sizeof(mqtt.sub_topics[2]), g_topic_control_cmd);
        mqtt.sub_topic_count = 3U;
        copy_string(mqtt.pub_topic, sizeof(mqtt.pub_topic), g_topic_heartbeat);
    }
    MQTT_Core_Init(&mqtt, cloud_on_message);
    MQTT_Core_SetAtCore(&g_at_core);

    EC_Power_Init();
    EC_Power_StartOnSequence();
    uart_start_rx();
    g_heartbeat_due = HAL_GetTick();
    g_status_report_due = HAL_GetTick();
    g_status_report_sequence = 1U;
    memset(&g_status_snapshot, 0, sizeof(g_status_snapshot));
    g_iccid_due = HAL_GetTick() + 1000U;
    g_initialized = 1U;
    if (g_config_ready) APP_LOGI("CLOUD", "configured vid=%s broker=%s:%u", cfg->vid, cfg->mqtt_host, cfg->mqtt_port);
    else APP_LOGW("CLOUD", "cloud disabled: provision config page");
}

void CloudService_Task(void)
{
    uint8_t ready;
    if (!g_initialized) return;
    EC_Power_Task();
    (void)ABoxEc800Rx_Drain(&g_ec800_rx, ec800_rx_sink, &g_at_core, 0U);
    EC800_AT_SetRxOverflowCount(&g_at_core,
                                ABoxEc800Rx_OverflowCount(&g_ec800_rx));
    if (ABoxEc800Rx_TakeRestartRequest(&g_ec800_rx))
    {
        ABoxEc800At_AbortAll(&g_at_core, ABOX_EC800_RESULT_ERROR);
        (void)HAL_UART_DMAStop(&huart1);
        ABoxEc800Rx_AfterRestart(&g_ec800_rx);
        uart_start_rx();
    }
    EC800_AT_Task(&g_at_core);
    OtaService_Task();
    if (g_ota_response_waiting && OtaService_TakeInstallReady())
    {
        char result[128];
        (void)snprintf(result, sizeof(result), "{\"fwVersion\":\"%s\",\"otaPhase\":\"INSTALL_PENDING\"}", OtaService_TargetVersion());
        if (schedule_response(g_topic_ota_resp, g_ota_request_id, 200U, "INSTALL_PENDING", result, 1U))
            g_ota_response_waiting = 0U;
    }
    if (g_ota_response_waiting)
    {
        uint32_t ota_error;
        if (OtaService_TakeFailure(&ota_error))
        {
            char result[64];
            (void)snprintf(result, sizeof(result), "{\"errorCode\":%lu}", (unsigned long)ota_error);
            if (schedule_response(g_topic_ota_resp, g_ota_request_id, 500U, "OTA_FAILED", result, 0U))
                g_ota_response_waiting = 0U;
        }
    }
    if (!g_config_ready || !EC_Power_IsOnDone()) return;
    MQTT_Core_Task();
    ready = MQTT_Core_IsReady();
    if (ready && !g_mqtt_was_ready)
    {
        g_heartbeat_due = HAL_GetTick();
        g_status_report_due = HAL_GetTick();
        g_ota_begin_due = HAL_GetTick() + 1000U;
        g_ota_begin_pending = 1U;
        APP_LOGI("CLOUD", "mqtt ready");
    }
    else if (!ready && g_mqtt_was_ready) APP_LOGW("CLOUD", "mqtt disconnected");
    g_mqtt_was_ready = ready;
    service_tx();
    if (ready && g_ota_begin_pending && (int32_t)(HAL_GetTick() - g_ota_begin_due) >= 0 &&
        !EC800_AT_IsBusy(&g_at_core))
    {
        if (OtaService_BeginProvisioning()) g_ota_begin_pending = 0U;
    }
    if (g_reboot_pending && (int32_t)(HAL_GetTick() - g_reboot_due) >= 0)
    {
        __disable_irq();
        NVIC_SystemReset();
    }
}

void CloudService_OnUartRxEvent(UART_HandleTypeDef* huart, uint16_t size)
{
    if ((huart == 0) || (huart->Instance != USART1)) return;
    (void)ABoxEc800Rx_OnDmaEvent(&g_ec800_rx, size);
}

void CloudService_OnUartError(UART_HandleTypeDef* huart)
{
    if ((huart == 0) || (huart->Instance != USART1)) return;
    ABoxEc800Rx_RequestRestart(&g_ec800_rx);
}
