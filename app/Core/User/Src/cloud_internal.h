#ifndef CLOUD_INTERNAL_H
#define CLOUD_INTERNAL_H

/* Cloud task owns this context. Only DMA receive callbacks touch ec800_rx.
 * Internal implementation details stay out of public service headers. */
#include <stdint.h>
#include "abox_ec800_at_legacy.h"
#include "abox_ec800_rx.h"
#include "app_log.h"
#include "cloud_protocol.h"
#include "cloud_telemetry.h"

#define CLOUD_RX_DMA_SIZE (EC800_AT_RAW_CHUNK_MAX + 64U)
#define CLOUD_RX_RING_SIZE 4096U
#define CLOUD_PAYLOAD_SIZE 1100U
#define CLOUD_PAYLOAD_LIMIT 1080U
#define CLOUD_TOPIC_SIZE 128U
#define CLOUD_REQUEST_ID_SIZE CLOUD_PROTOCOL_REQUEST_ID_SIZE
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

typedef struct {
    Ec800AtCore_t at_core;
    uint8_t uart_dma[CLOUD_RX_DMA_SIZE];
    uint8_t rx_ring[CLOUD_RX_RING_SIZE];
    ABoxEc800Rx ec800_rx;

    char topic_heartbeat[CLOUD_TOPIC_SIZE];
    char topic_status_report[CLOUD_TOPIC_SIZE];
    char topic_config_cmd[CLOUD_TOPIC_SIZE];
    char topic_config_resp[CLOUD_TOPIC_SIZE];
    char topic_ota_cmd[CLOUD_TOPIC_SIZE];
    char topic_ota_resp[CLOUD_TOPIC_SIZE];
    char topic_log[CLOUD_TOPIC_SIZE];
    char topic_control_cmd[CLOUD_TOPIC_SIZE];
    char topic_control_resp[CLOUD_TOPIC_SIZE];

    uint8_t initialized;
    uint8_t config_ready;
    uint8_t mqtt_was_ready;
    uint8_t reset_reason;
    uint8_t ota_response_waiting;
    uint8_t ota_begin_pending;
    char ota_request_id[CLOUD_REQUEST_ID_SIZE];
    uint32_t heartbeat_due;
    uint32_t status_report_due;
    uint32_t status_report_sequence;
    CloudTelemetry telemetry;
    ChassisStatusSnapshot_t status_snapshot;
    uint32_t iccid_due;
    uint32_t ota_begin_due;

    CloudTxKind_t tx_kind;
    uint32_t tx_start_sequence;
    char tx_payload[CLOUD_PAYLOAD_SIZE];

    uint8_t response_pending;
    uint8_t response_reboots;
    uint8_t response_ack_rollback;
    char response_request_id[CLOUD_REQUEST_ID_SIZE];
    char response_topic[CLOUD_TOPIC_SIZE];
    char response_payload[CLOUD_PAYLOAD_SIZE];

    char last_request_id[CLOUD_REQUEST_ID_SIZE];
    char last_response_topic[CLOUD_TOPIC_SIZE];
    char last_response_payload[CLOUD_PAYLOAD_SIZE];

    char log_request_id[CLOUD_REQUEST_ID_SIZE];
    char log_snapshot[CLOUD_LOG_SNAPSHOT_SIZE];
    uint16_t log_line_offset[CLOUD_LOG_MAX_LINES];
    uint16_t log_line_length[CLOUD_LOG_MAX_LINES];
    uint8_t log_chunk_start[CLOUD_LOG_MAX_LINES];
    uint8_t log_chunk_count[CLOUD_LOG_MAX_LINES];
    uint8_t log_line_count;
    uint8_t log_chunk_total;
    uint8_t log_chunk_index;
    uint8_t log_active;

    uint8_t alert_pending;
    char alert_payload[CLOUD_ALERT_PAYLOAD_SIZE];
    uint8_t reboot_pending;
    uint32_t reboot_due;

} CloudContext;

extern CloudContext g_cloud;

void CloudMessages_CopyString(char* dst, uint16_t size, const char* src);
void CloudMessages_CacheResponse(const char* request_id, const char* topic, const char* payload);
uint8_t CloudMessages_ScheduleResponse(const char* topic, const char* request_id, uint16_t code, const char* message,
                                 const char* result_json, uint8_t reboot);
void CloudMessages_ScheduleSimpleError(const char* topic, const char* request_id, uint16_t code, const char* message);
uint8_t CloudMessages_ScheduleRawResponse(const char* topic, const char* payload);
void CloudMessages_BuildInfoResult(char* output, uint16_t size);
uint8_t CloudMessages_BuildHeartbeat(char* output, uint16_t size);
void CloudMessages_RememberStatusSnapshot(uint32_t now);
const char* CloudMessages_StatusChangeReason(uint32_t now);
uint8_t CloudMessages_BuildStatusReport(char* output, uint16_t size, const char* report_reason);
void CloudMessages_PrepareLogSnapshot(const char* request_id);
uint8_t CloudMessages_BuildLogChunk(char* output, uint16_t size);
uint8_t CloudMessages_BuildAlert(char* output, uint16_t size, const char* line, AppLogLevel_t level);
void CloudMessages_BuildCalibrationResult(char* output, uint16_t size);
void CloudCommands_OnMessage(const char* topic, const char* payload);
void CloudTx_Service(void);
uint32_t CloudPort_Now(void);
#endif
