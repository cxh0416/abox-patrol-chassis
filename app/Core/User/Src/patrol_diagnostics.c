#include "patrol_diagnostics.h"

#include <string.h>
#include <stdio.h>

#define PATROL_DIAG_SDO_TIMEOUT_MS 60U
#define PATROL_DIAG_SDO_RETRY_COUNT 1U

typedef struct {
    uint8_t node;
    uint16_t index;
    uint8_t subindex;
    uint16_t period_ms;
} PatrolSdoEntry;

static const PatrolSdoEntry g_sdo_entries[PATROL_DIAG_SDO_ENTRY_COUNT] = {
    {1U, 0x6041U, 0U, 100U},
    {1U, 0x6064U, 0U, 50U},
    {2U, 0x6041U, 0U, 100U},
    {2U, 0x6069U, 0U, 50U},
    {2U, 0x6869U, 0U, 50U},
    {2U, 0x2000U, 0U, 1000U},
    {2U, 0x2001U, 0U, 1000U},
    {2U, 0x2002U, 0U, 1000U},
    {2U, 0x2003U, 0U, 1000U},
    {2U, 0x2004U, 0U, 1000U},
    {2U, 0x2005U, 0U, 1000U},
    {2U, 0x2006U, 0U, 1000U},
};

static uint8_t due(uint32_t now_ms, uint32_t due_ms)
{
    return (uint8_t)((int32_t)(now_ms - due_ms) >= 0);
}

static uint8_t fresh(uint8_t seen, uint32_t sample_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    return (uint8_t)(seen && (now_ms - sample_ms) <= timeout_ms);
}

static uint8_t sdo_node_ready(const PatrolDiagnostics* d, uint8_t node, uint32_t now_ms)
{
    uint8_t seen = node == 1U ? d->steering_heartbeat_seen : d->drive_heartbeat_seen;
    uint8_t state = node == 1U ? d->steering_heartbeat_state : d->drive_heartbeat_state;
    uint32_t stamp = node == 1U ? d->steering_heartbeat_ms : d->drive_heartbeat_ms;
    /* Wait for an existing peer to announce Pre-Operational/Operational.
     * An unpowered bus cannot ACK a request; do not queue retransmissions. */
    return (uint8_t)(fresh(seen, stamp, now_ms, PATROL_DIAG_HEARTBEAT_TIMEOUT_MS) &&
                     (state == 0x7FU || state == 0x05U));
}

static void finish_pending(PatrolDiagnostics* diagnostics, uint32_t now_ms)
{
    uint8_t entry = diagnostics->pending_entry;
    diagnostics->sdo_due_ms[entry] = now_ms + g_sdo_entries[entry].period_ms;
    diagnostics->pending_valid = 0U;
    diagnostics->pending_retry = 0U;
}

static void store_sdo_value(PatrolDiagnostics* diagnostics, const PatrolSdoEntry* entry,
                            int32_t value, uint32_t now_ms)
{
    if (entry->node == 1U && entry->index == 0x6041U)
    {
        diagnostics->steering_status_word = (uint16_t)value;
        diagnostics->steering_status_ms = now_ms;
    }
    else if (entry->node == 1U && entry->index == 0x6064U)
    {
        diagnostics->steering_position_count = value;
        diagnostics->steering_position_ms = now_ms;
        diagnostics->steering_position_seen = 1U;
    }
    else if (entry->node == 2U && entry->index == 0x6041U)
    {
        diagnostics->drive_status_word = (uint16_t)value;
        diagnostics->drive_status_ms = now_ms;
    }
    else if (entry->node == 2U && entry->index == 0x6069U)
    {
        diagnostics->left_actual_rpm = value;
        diagnostics->left_rpm_ms = now_ms;
        diagnostics->left_rpm_seen = 1U;
    }
    else if (entry->node == 2U && entry->index == 0x6869U)
    {
        diagnostics->right_actual_rpm = value;
        diagnostics->right_rpm_ms = now_ms;
        diagnostics->right_rpm_seen = 1U;
    }
    else if (entry->node == 2U && entry->index >= 0x2000U && entry->index <= 0x2006U)
    {
        diagnostics->drive_objects[entry->index - 0x2000U] = value;
    }
}

void PatrolDiagnostics_Init(PatrolDiagnostics* diagnostics)
{
    if (diagnostics == 0) return;
    memset(diagnostics, 0, sizeof(*diagnostics));
}

void PatrolDiagnostics_OnCanFrame(PatrolDiagnostics* diagnostics, const PatrolCanFrame* frame,
                                  uint32_t now_ms)
{
    const PatrolSdoEntry* entry;
    int32_t value;
    uint8_t size;
    if (diagnostics == 0 || frame == 0) return;
    diagnostics->can_rx_count++;
    if (frame->id == 0x701U && frame->dlc >= 1U)
    {
        diagnostics->steering_heartbeat_seen = 1U;
        diagnostics->steering_heartbeat_state = frame->data[0];
        diagnostics->steering_heartbeat_ms = now_ms;
        return;
    }
    if (frame->id == 0x702U && frame->dlc >= 1U)
    {
        diagnostics->drive_heartbeat_seen = 1U;
        diagnostics->drive_heartbeat_state = frame->data[0];
        diagnostics->drive_heartbeat_ms = now_ms;
        return;
    }
    if (!diagnostics->pending_valid)
    {
        diagnostics->can_ignored_count++;
        return;
    }
    entry = &g_sdo_entries[diagnostics->pending_entry];
    if (frame->id != (uint16_t)(0x580U + entry->node) || frame->dlc < 4U || frame->dlc > 8U ||
        frame->data[1] != (uint8_t)entry->index || frame->data[2] != (uint8_t)(entry->index >> 8) ||
        frame->data[3] != entry->subindex)
    {
        diagnostics->can_ignored_count++;
        return;
    }
    if (frame->data[0] == 0x80U)
    {
        if (frame->dlc != 8U)
        {
            diagnostics->can_ignored_count++;
            return;
        }
        diagnostics->last_sdo_abort_code = (uint32_t)frame->data[4] |
            (uint32_t)frame->data[5] << 8 | (uint32_t)frame->data[6] << 16 |
            (uint32_t)frame->data[7] << 24;
        diagnostics->sdo_abort_count++;
        diagnostics->sdo_abort_mask |= (uint16_t)(1U << diagnostics->pending_entry);
        finish_pending(diagnostics, now_ms);
        return;
    }
    if (PatrolCanopen_ParseSdoValue(frame, entry->node, entry->index, entry->subindex,
                                    &value, &size))
    {
        (void)size;
        diagnostics->sdo_success_count++;
        diagnostics->sdo_success_mask |= (uint16_t)(1U << diagnostics->pending_entry);
        store_sdo_value(diagnostics, entry, value, now_ms);
        finish_pending(diagnostics, now_ms);
    }
    else
    {
        diagnostics->can_ignored_count++;
    }
}

uint8_t PatrolDiagnostics_TakeSdoRead(PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                     PatrolCanFrame* frame)
{
    uint8_t offset;
    if (diagnostics == 0 || frame == 0) return 0U;
    if (diagnostics->pending_valid)
    {
        const PatrolSdoEntry* entry = &g_sdo_entries[diagnostics->pending_entry];
        if (!sdo_node_ready(diagnostics, entry->node, now_ms))
        {
            finish_pending(diagnostics, now_ms);
            return 0U;
        }
        if ((now_ms - diagnostics->pending_sent_ms) < PATROL_DIAG_SDO_TIMEOUT_MS) return 0U;
        diagnostics->sdo_timeout_count++;
        if (diagnostics->pending_retry >= PATROL_DIAG_SDO_RETRY_COUNT)
        {
            finish_pending(diagnostics, now_ms);
            return 0U;
        }
        diagnostics->pending_retry++;
        diagnostics->pending_sent_ms = now_ms;
        *frame = PatrolCanopen_SdoRead(entry->node, entry->index, entry->subindex);
        return 1U;
    }
    for (offset = 0U; offset < PATROL_DIAG_SDO_ENTRY_COUNT; offset++)
    {
        uint8_t index = (uint8_t)((diagnostics->next_sdo_entry + offset) % PATROL_DIAG_SDO_ENTRY_COUNT);
        if (diagnostics->motion_priority && index >= 6U) continue;
        const PatrolSdoEntry* entry = &g_sdo_entries[index];
        if (!sdo_node_ready(diagnostics, entry->node, now_ms)) continue;
        if (!due(now_ms, diagnostics->sdo_due_ms[index])) continue;
        diagnostics->pending_valid = 1U;
        diagnostics->pending_entry = index;
        diagnostics->pending_retry = 0U;
        diagnostics->pending_sent_ms = now_ms;
        diagnostics->next_sdo_entry = (uint8_t)((index + 1U) % PATROL_DIAG_SDO_ENTRY_COUNT);
        *frame = PatrolCanopen_SdoRead(entry->node, entry->index, entry->subindex);
        return 1U;
    }
    return 0U;
}

void PatrolDiagnostics_OnSdoTxError(PatrolDiagnostics* diagnostics, uint32_t now_ms)
{
    if (diagnostics == 0) return;
    diagnostics->sdo_tx_error_count++;
    if (diagnostics->pending_valid) diagnostics->pending_sent_ms = now_ms - PATROL_DIAG_SDO_TIMEOUT_MS;
}

void PatrolDiagnostics_OnEncoderConfig(PatrolDiagnostics* diagnostics, const uint16_t registers[3],
                                       uint32_t now_ms)
{
    if (diagnostics == 0 || registers == 0) return;
    memcpy(diagnostics->encoder_config, registers, sizeof(diagnostics->encoder_config));
    diagnostics->encoder_config_valid = 1U;
    diagnostics->encoder_config_ms = now_ms;
}

void PatrolDiagnostics_OnEncoderCount(PatrolDiagnostics* diagnostics, uint32_t count,
                                      uint32_t now_ms)
{
    if (diagnostics == 0) return;
    diagnostics->encoder_count = count;
    diagnostics->encoder_ms = now_ms;
    diagnostics->encoder_seen = 1U;
}

void PatrolDiagnostics_OnEncoderError(PatrolDiagnostics* diagnostics, PatrolEncoderError error)
{
    if (diagnostics == 0) return;
    if (error == PATROL_ENCODER_ERROR_TIMEOUT) diagnostics->encoder_timeout_count++;
    else if (error == PATROL_ENCODER_ERROR_CRC) diagnostics->encoder_crc_error_count++;
    else if (error == PATROL_ENCODER_ERROR_EXCEPTION) diagnostics->encoder_exception_count++;
    else if (error == PATROL_ENCODER_ERROR_FORMAT) diagnostics->encoder_format_error_count++;
    else diagnostics->encoder_transport_error_count++;
}

void PatrolDiagnostics_Snapshot(const PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                PatrolDiagnosticsSnapshot* snapshot)
{
    uint32_t oldest;
    uint8_t all_feedback;
    if (diagnostics == 0 || snapshot == 0) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->steering_online = fresh(diagnostics->steering_heartbeat_seen,
        diagnostics->steering_heartbeat_ms, now_ms, PATROL_DIAG_HEARTBEAT_TIMEOUT_MS);
    snapshot->drive_online = fresh(diagnostics->drive_heartbeat_seen,
        diagnostics->drive_heartbeat_ms, now_ms, PATROL_DIAG_HEARTBEAT_TIMEOUT_MS);
    snapshot->steering_feedback_fresh = fresh(diagnostics->steering_position_seen,
        diagnostics->steering_position_ms, now_ms, PATROL_DIAG_FEEDBACK_TIMEOUT_MS);
    snapshot->drive_feedback_fresh = (uint8_t)(fresh(diagnostics->left_rpm_seen,
        diagnostics->left_rpm_ms, now_ms, PATROL_DIAG_FEEDBACK_TIMEOUT_MS) &&
        fresh(diagnostics->right_rpm_seen, diagnostics->right_rpm_ms, now_ms,
              PATROL_DIAG_FEEDBACK_TIMEOUT_MS));
    snapshot->encoder_online = fresh(diagnostics->encoder_seen, diagnostics->encoder_ms,
        now_ms, PATROL_DIAG_FEEDBACK_TIMEOUT_MS);
    snapshot->encoder_config_valid = diagnostics->encoder_config_valid;
    snapshot->drive_fault = (uint8_t)(((diagnostics->steering_status_word |
        diagnostics->drive_status_word) & 0x0008U) != 0U || diagnostics->drive_objects[0] != 0);
    snapshot->steering_heartbeat_state = diagnostics->steering_heartbeat_state;
    snapshot->drive_heartbeat_state = diagnostics->drive_heartbeat_state;
    snapshot->steering_status_word = diagnostics->steering_status_word;
    snapshot->drive_status_word = diagnostics->drive_status_word;
    snapshot->steering_position_count = diagnostics->steering_position_count;
    snapshot->left_actual_rpm = diagnostics->left_actual_rpm;
    snapshot->right_actual_rpm = diagnostics->right_actual_rpm;
    snapshot->encoder_count = diagnostics->encoder_count;
    all_feedback = (uint8_t)(snapshot->steering_feedback_fresh && snapshot->drive_feedback_fresh &&
                             snapshot->encoder_online);
    if (all_feedback)
    {
        oldest = diagnostics->steering_position_ms;
        if ((int32_t)(diagnostics->left_rpm_ms - oldest) < 0) oldest = diagnostics->left_rpm_ms;
        if ((int32_t)(diagnostics->right_rpm_ms - oldest) < 0) oldest = diagnostics->right_rpm_ms;
        if ((int32_t)(diagnostics->encoder_ms - oldest) < 0) oldest = diagnostics->encoder_ms;
        snapshot->coherent_feedback_ms = oldest;
    }
    snapshot->can_rx_count = diagnostics->can_rx_count;
    snapshot->can_ignored_count = diagnostics->can_ignored_count;
    snapshot->sdo_success_count = diagnostics->sdo_success_count;
    snapshot->sdo_success_mask = diagnostics->sdo_success_mask;
    snapshot->sdo_abort_mask = diagnostics->sdo_abort_mask;
    snapshot->sdo_tx_error_count = diagnostics->sdo_tx_error_count;
    memcpy(snapshot->drive_objects, diagnostics->drive_objects, sizeof(snapshot->drive_objects));
    snapshot->sdo_timeout_count = diagnostics->sdo_timeout_count;
    snapshot->sdo_abort_count = diagnostics->sdo_abort_count;
    snapshot->last_sdo_abort_code = diagnostics->last_sdo_abort_code;
    snapshot->encoder_timeout_count = diagnostics->encoder_timeout_count;
    snapshot->encoder_crc_error_count = diagnostics->encoder_crc_error_count;
    snapshot->encoder_exception_count = diagnostics->encoder_exception_count;
}

uint8_t PatrolDiagnostics_FormatCanJson(const PatrolDiagnosticsSnapshot* s,
    uint8_t bus_off, uint8_t bus_off_active, uint32_t rx_overflow,
    uint32_t can1_raw_rx, uint32_t can2_raw_rx, char* output, uint16_t size)
{
    int length;
    if (s == 0 || output == 0 || size == 0U) return 0U;
    length = snprintf(output, size,
        "{\"canRx\":%lu,\"can1RawRx\":%lu,\"can2RawRx\":%lu,\"canIgnored\":%lu,\"canRxOverflow\":%lu,"
        "\"canBusOff\":%s,\"canBusOffActive\":%s,"
        "\"steerHb\":%s,\"driveHb\":%s,\"steerNmt\":%u,\"driveNmt\":%u,"
        "\"steerFresh\":%s,\"driveFresh\":%s,\"steerRaw\":%ld,\"leftRpm\":%ld,\"rightRpm\":%ld,"
        "\"steerSw\":%u,\"driveSw\":%u,\"sdoOk\":%lu,\"sdoReadMask\":%u,\"sdoAbortMask\":%u,"
        "\"sdoTimeouts\":%lu,\"sdoAborts\":%lu,\"lastAbort\":%lu,\"sdoTxErrors\":%lu,"
        "\"driveObjects\":[%ld,%ld,%ld,%ld,%ld,%ld,%ld]}",
        (unsigned long)s->can_rx_count, (unsigned long)can1_raw_rx, (unsigned long)can2_raw_rx,
        (unsigned long)s->can_ignored_count,
        (unsigned long)rx_overflow, bus_off ? "true" : "false", bus_off_active ? "true" : "false",
        s->steering_online ? "true" : "false", s->drive_online ? "true" : "false",
        (unsigned int)s->steering_heartbeat_state, (unsigned int)s->drive_heartbeat_state,
        s->steering_feedback_fresh ? "true" : "false", s->drive_feedback_fresh ? "true" : "false",
        (long)s->steering_position_count, (long)s->left_actual_rpm, (long)s->right_actual_rpm,
        (unsigned int)s->steering_status_word, (unsigned int)s->drive_status_word,
        (unsigned long)s->sdo_success_count, (unsigned int)s->sdo_success_mask,
        (unsigned int)s->sdo_abort_mask, (unsigned long)s->sdo_timeout_count,
        (unsigned long)s->sdo_abort_count, (unsigned long)s->last_sdo_abort_code,
        (unsigned long)s->sdo_tx_error_count,
        (long)s->drive_objects[0], (long)s->drive_objects[1], (long)s->drive_objects[2],
        (long)s->drive_objects[3], (long)s->drive_objects[4], (long)s->drive_objects[5],
        (long)s->drive_objects[6]);
    return (uint8_t)(length >= 0 && length < size);
}
