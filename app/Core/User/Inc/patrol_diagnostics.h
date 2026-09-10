#ifndef PATROL_CHASSIS_DIAGNOSTICS_H
#define PATROL_CHASSIS_DIAGNOSTICS_H

#include <stdint.h>

#include "patrol_canopen.h"

#define PATROL_DIAG_SDO_ENTRY_COUNT 12U
#define PATROL_DIAG_ENCODER_CONFIG_REGS 3U
#define PATROL_DIAG_HEARTBEAT_TIMEOUT_MS 3000U
#define PATROL_DIAG_FEEDBACK_TIMEOUT_MS 250U

typedef enum {
    PATROL_ENCODER_ERROR_TIMEOUT = 1,
    PATROL_ENCODER_ERROR_CRC,
    PATROL_ENCODER_ERROR_EXCEPTION,
    PATROL_ENCODER_ERROR_FORMAT,
    PATROL_ENCODER_ERROR_TRANSPORT
} PatrolEncoderError;

typedef struct {
    uint32_t can_rx_count;
    uint32_t can_ignored_count;
    uint32_t steering_heartbeat_ms;
    uint32_t drive_heartbeat_ms;
    uint32_t steering_status_ms;
    uint32_t drive_status_ms;
    uint32_t steering_position_ms;
    uint32_t left_rpm_ms;
    uint32_t right_rpm_ms;
    uint32_t encoder_ms;
    uint32_t encoder_config_ms;
    uint32_t sdo_timeout_count;
    uint32_t sdo_success_count;
    uint16_t sdo_success_mask;
    uint16_t sdo_abort_mask;
    uint32_t sdo_abort_count;
    uint32_t sdo_tx_error_count;
    uint32_t last_sdo_abort_code;
    uint32_t encoder_timeout_count;
    uint32_t encoder_crc_error_count;
    uint32_t encoder_exception_count;
    uint32_t encoder_format_error_count;
    uint32_t encoder_transport_error_count;
    uint8_t last_encoder_error;
    uint32_t encoder_count;
    int32_t steering_position_count;
    int32_t left_actual_rpm;
    int32_t right_actual_rpm;
    int32_t drive_objects[7];
    uint16_t steering_status_word;
    uint16_t drive_status_word;
    uint16_t encoder_config[PATROL_DIAG_ENCODER_CONFIG_REGS];
    uint8_t steering_heartbeat_state;
    uint8_t drive_heartbeat_state;
    uint8_t steering_heartbeat_seen;
    uint8_t drive_heartbeat_seen;
    uint8_t steering_position_seen;
    uint8_t left_rpm_seen;
    uint8_t right_rpm_seen;
    uint8_t encoder_seen;
    uint8_t encoder_config_valid;
    uint8_t pending_valid;
    uint8_t pending_entry;
    uint8_t pending_retry;
    uint32_t pending_sent_ms;
    uint32_t sdo_due_ms[PATROL_DIAG_SDO_ENTRY_COUNT];
    uint8_t next_sdo_entry;
    uint8_t motion_priority;
} PatrolDiagnostics;

typedef struct {
    uint8_t steering_online;
    uint8_t drive_online;
    uint8_t steering_feedback_fresh;
    uint8_t drive_feedback_fresh;
    uint8_t steering_status_fresh;
    uint8_t drive_status_fresh;
    uint8_t encoder_online;
    uint8_t encoder_config_valid;
    uint8_t drive_fault;
    uint8_t steering_heartbeat_state;
    uint8_t drive_heartbeat_state;
    uint16_t steering_status_word;
    uint16_t drive_status_word;
    int32_t steering_position_count;
    int32_t left_actual_rpm;
    int32_t right_actual_rpm;
    uint32_t encoder_count;
    uint32_t encoder_sample_ms;
    uint32_t coherent_feedback_ms;
    uint32_t can_rx_count;
    uint32_t can_ignored_count;
    uint32_t sdo_success_count;
    uint16_t sdo_success_mask;
    uint16_t sdo_abort_mask;
    uint32_t sdo_tx_error_count;
    int32_t drive_objects[7];
    uint32_t sdo_timeout_count;
    uint32_t sdo_abort_count;
    uint32_t last_sdo_abort_code;
    uint32_t encoder_timeout_count;
    uint32_t encoder_crc_error_count;
    uint32_t encoder_exception_count;
    uint32_t encoder_format_error_count;
    uint32_t encoder_transport_error_count;
    uint8_t last_encoder_error;
} PatrolDiagnosticsSnapshot;

void PatrolDiagnostics_Init(PatrolDiagnostics* diagnostics);
void PatrolDiagnostics_OnCanFrame(PatrolDiagnostics* diagnostics, const PatrolCanFrame* frame,
                                  uint32_t now_ms);
uint8_t PatrolDiagnostics_TakeSdoRead(PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                     PatrolCanFrame* frame);
void PatrolDiagnostics_OnSdoTxError(PatrolDiagnostics* diagnostics, uint32_t now_ms);
void PatrolDiagnostics_OnEncoderConfig(PatrolDiagnostics* diagnostics, const uint16_t registers[3],
                                       uint32_t now_ms);
void PatrolDiagnostics_OnEncoderCount(PatrolDiagnostics* diagnostics, uint32_t count,
                                      uint32_t now_ms);
void PatrolDiagnostics_OnEncoderError(PatrolDiagnostics* diagnostics, PatrolEncoderError error);
void PatrolDiagnostics_Snapshot(const PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                PatrolDiagnosticsSnapshot* snapshot);
uint8_t PatrolDiagnostics_FormatCanJson(const PatrolDiagnosticsSnapshot* snapshot,
    uint8_t bus_off, uint8_t bus_off_active, uint32_t rx_overflow,
    uint32_t can1_raw_rx, uint32_t can2_raw_rx, char* output, uint16_t size);

#endif
