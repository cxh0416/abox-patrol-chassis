#include "patrol_diagnostics.h"

#include <string.h>
#include <stdio.h>

#define PATROL_DIAG_SDO_TIMEOUT_MS 60U
#define PATROL_DIAG_SDO_RETRY_COUNT 1U
#define PATROL_STEERING_TPDO_FRESH_MS 100U
#define PATROL_STEERING_POSITION_FALLBACK_MS 250U
/* A retry must fit before any safety sample expires, including two 10ms task
 * turns for response processing and resuming the other feedback reads. */
#define PATROL_DIAG_RETRY_FEEDBACK_MAX_AGE_MS \
    (PATROL_DIAG_FEEDBACK_TIMEOUT_MS - PATROL_DIAG_SDO_TIMEOUT_MS - 20U)

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

// 判断无符号毫秒时钟下调度期限是否到达。通过带符号差值比较支持时钟自然回绕，要求期限差处于可比较的半周期范围。
static uint8_t due(uint32_t now_ms, uint32_t due_ms)
{
    return (uint8_t)((int32_t)(now_ms - due_ms) >= 0);
}

// 判断一个已出现的采样是否仍在允许时效内。未见样本或年龄超过 timeout_ms 均无效，不用默认零值伪造新鲜反馈。
static uint8_t fresh(uint8_t seen, uint32_t sample_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    return (uint8_t)(seen && (now_ms - sample_ms) <= timeout_ms);
}

// 选择对象下一次读取周期。Node 1 的 0x281 TPDO 正常时，0x6064 SDO
// 只做低频兜底；TPDO 中断后恢复 50 ms 快速轮询。
static uint32_t sdo_period_ms(const PatrolDiagnostics* diagnostics,
                              const PatrolSdoEntry* entry, uint32_t now_ms)
{
    if (entry->node == 1U && entry->index == 0x6064U &&
        diagnostics->steering_tpdo_position_ms != 0U &&
        fresh(1U, diagnostics->steering_tpdo_position_ms, now_ms,
              PATROL_STEERING_TPDO_FRESH_MS))
        return PATROL_STEERING_POSITION_FALLBACK_MS;
    return entry->period_ms;
}

// 检查节点在线及当前时间下的诊断读取资格。供只读轮询选择节点使用，不代表节点已取得运动使能。
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

// 为一次只读重试核对双节点、状态、双轮、转角及编码器的真实样本。预留完整事务和调度余量；不能用重发时间延长旧反馈，也不能在驱动故障时消耗恢复时间。
static uint8_t retry_feedback_ready(const PatrolDiagnostics* d, uint32_t now_ms)
{
    const uint32_t limit = PATROL_DIAG_RETRY_FEEDBACK_MAX_AGE_MS;
    return (uint8_t)(sdo_node_ready(d, 1U, now_ms) && sdo_node_ready(d, 2U, now_ms) &&
        !((d->steering_status_word | d->drive_status_word) & 8U) &&
        !d->drive_objects[0] && !d->drive_objects[1] &&
        fresh(d->steering_status_word != 0U, d->steering_status_ms, now_ms, limit) &&
        fresh(d->drive_status_word != 0U, d->drive_status_ms, now_ms, limit) &&
        fresh(d->steering_position_seen, d->steering_position_ms, now_ms, limit) &&
        fresh(d->left_rpm_seen, d->left_rpm_ms, now_ms, limit) &&
        fresh(d->right_rpm_seen, d->right_rpm_ms, now_ms, limit) &&
        fresh(d->encoder_seen, d->encoder_ms, now_ms, limit));
}

// 返回关键反馈样本的年龄；未见样本视为无限旧，不能由默认零值伪造新鲜度。
static uint32_t feedback_age(const PatrolDiagnostics* diagnostics, uint8_t entry, uint32_t now_ms)
{
    uint8_t seen;
    uint32_t stamp;
    switch (entry) {
    case 0U:
        seen = diagnostics->steering_status_word != 0U;
        stamp = diagnostics->steering_status_ms;
        break;
    case 1U:
        seen = diagnostics->steering_position_seen;
        stamp = diagnostics->steering_position_ms;
        break;
    case 2U:
        seen = diagnostics->drive_status_word != 0U;
        stamp = diagnostics->drive_status_ms;
        break;
    case 3U:
        seen = diagnostics->left_rpm_seen;
        stamp = diagnostics->left_rpm_ms;
        break;
    case 4U:
        seen = diagnostics->right_rpm_seen;
        stamp = diagnostics->right_rpm_ms;
        break;
    default:
        return 0U;
    }
    return seen ? now_ms - stamp : UINT32_MAX;
}

// 选择年龄最大的关键反馈对象；转向位置仍由新鲜 TPDO 优先供给，避免无意义的 SDO 抢占。
static uint8_t urgent_feedback_entry(const PatrolDiagnostics* diagnostics, uint32_t now_ms)
{
    uint8_t selected = UINT8_MAX;
    uint32_t oldest = 0U;
    for (uint8_t entry = 0U; entry < 5U; entry++) {
        const PatrolSdoEntry* sdo = &g_sdo_entries[entry];
        uint32_t age;
        if (!sdo_node_ready(diagnostics, sdo->node, now_ms)) continue;
        if (!due(now_ms, diagnostics->sdo_due_ms[entry])) continue;
        if (entry == 1U && diagnostics->steering_tpdo_position_ms != 0U &&
            fresh(1U, diagnostics->steering_tpdo_position_ms, now_ms,
                  PATROL_STEERING_TPDO_FRESH_MS)) continue;
        age = feedback_age(diagnostics, entry, now_ms);
        if (age < PATROL_DIAG_FEEDBACK_PRIORITY_MS) continue;
        if (selected == UINT8_MAX || age > oldest) {
            selected = entry;
            oldest = age;
        }
    }
    return selected;
}

// 启动一个指定诊断对象的事务。调用方已确认节点可用且当前没有其他 SDO 所有者。
static uint8_t take_entry(PatrolDiagnostics* diagnostics, uint8_t entry,
                          uint32_t now_ms, PatrolCanFrame* frame)
{
    const PatrolSdoEntry* sdo = &g_sdo_entries[entry];
    diagnostics->pending_valid = 1U;
    diagnostics->pending_entry = entry;
    diagnostics->pending_retry = 0U;
    diagnostics->pending_sent_ms = now_ms;
    diagnostics->next_sdo_entry = (uint8_t)((entry + 1U) % PATROL_DIAG_SDO_ENTRY_COUNT);
    *frame = PatrolCanopen_SdoRead(sdo->node, sdo->index, sdo->subindex);
    return 1U;
}

// 结束当前诊断 SDO 事务并安排后续轮询。清理在途标志及相关时间状态，避免旧响应阻塞后续安全反馈读取。
static void finish_pending(PatrolDiagnostics* diagnostics, uint32_t now_ms)
{
    uint8_t entry = diagnostics->pending_entry;
    diagnostics->sdo_due_ms[entry] =
        now_ms + sdo_period_ms(diagnostics, &g_sdo_entries[entry], now_ms);
    diagnostics->pending_valid = 0U;
    diagnostics->pending_retry = 0U;
}

// 把已匹配的对象字典反馈写入诊断缓存。按轮询表条目更新数值与各自采样时间，左右轮和状态字的新鲜度分别维护。
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
        if (entry->index <= 0x2001U) diagnostics->drive_code_ms[entry->index - 0x2000U] = now_ms;
    }
}

// 初始化只读运动诊断状态与轮询进度。清空在线、反馈、错误和在途事务，不发送写对象请求。
void PatrolDiagnostics_Init(PatrolDiagnostics* diagnostics)
{
    if (diagnostics == 0) return;
    memset(diagnostics, 0, sizeof(*diagnostics));
}

// 消费转向驱动器已核验映射的 TPDO1/TPDO2。状态字和位置由驱动器主动周期发送，
// 不应因执行器占用 SDO 或单次诊断读超时而错误过期。
uint8_t PatrolDiagnostics_OnTpdoFrame(PatrolDiagnostics* diagnostics,
                                      const PatrolCanFrame* frame, uint32_t now_ms)
{
    if (diagnostics == 0 || frame == 0) return 0U;
    if (frame->id == 0x181U && frame->dlc >= 7U) {
        diagnostics->steering_status_word = (uint16_t)frame->data[0] |
            (uint16_t)frame->data[1] << 8;
        diagnostics->steering_status_ms = now_ms;
        return 1U;
    }
    if (frame->id == 0x281U && frame->dlc >= 4U) {
        diagnostics->steering_position_count = (int32_t)((uint32_t)frame->data[0] |
            (uint32_t)frame->data[1] << 8 | (uint32_t)frame->data[2] << 16 |
            (uint32_t)frame->data[3] << 24);
        diagnostics->steering_position_ms = now_ms;
        diagnostics->steering_tpdo_position_ms = now_ms;
        diagnostics->steering_position_seen = 1U;
        return 1U;
    }
    return 0U;
}

// 解析节点心跳、PDO 和匹配的诊断 SDO 响应。根据有效报文更新对应缓存与时间戳，保留 Abort 和协议错误证据，不把无关帧算作目标对象响应。
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

// 取出只读 SDO 并处理有限重试。一次超时保留计数；只有调用方安全门禁和本模块真实反馈余量均允许时才重读，耗尽后释放事务服务其他对象，由既有安全门禁判断是否停车。
uint8_t PatrolDiagnostics_TakeSdoRead(PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                     PatrolCanFrame* frame, uint8_t allow_retry)
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
        if (diagnostics->pending_retry >= PATROL_DIAG_SDO_RETRY_COUNT ||
            !allow_retry || !retry_feedback_ready(diagnostics, now_ms))
        {
            finish_pending(diagnostics, now_ms);
            return 0U;
        }
        diagnostics->pending_retry++;
        diagnostics->pending_sent_ms = now_ms;
        *frame = PatrolCanopen_SdoRead(entry->node, entry->index, entry->subindex);
        return 1U;
    }
    /* During the bounded stop, serve the oldest due safety sample first.
     * Shutdown status polling must not starve the other node's feedback. */
    if (diagnostics->stop_priority) {
        const uint32_t stamps[5] = {diagnostics->steering_status_ms,
            diagnostics->steering_position_ms, diagnostics->drive_status_ms,
            diagnostics->left_rpm_ms, diagnostics->right_rpm_ms};
        uint32_t oldest_age = 0U;
        uint8_t selected = 0U;
        for (uint8_t i = 0U; i < 5U; i++) {
            if (!sdo_node_ready(diagnostics, g_sdo_entries[i].node, now_ms) ||
                !due(now_ms, diagnostics->sdo_due_ms[i])) continue;
            uint32_t age = now_ms - stamps[i];
            if (!selected || age > oldest_age) {
                selected = 1U; oldest_age = age; diagnostics->next_sdo_entry = i;
            }
        }
    }
    /* Sample position, wheel speeds and status before low-rate diagnostics.
     * Each transaction still has one owner and its bounded retry policy. */
    for (uint8_t priority = 0U; priority < 2U; priority++)
    for (offset = 0U; offset < PATROL_DIAG_SDO_ENTRY_COUNT; offset++)
    {
        uint8_t index = (uint8_t)((diagnostics->next_sdo_entry + offset) % PATROL_DIAG_SDO_ENTRY_COUNT);
        if ((index < 5U) != (priority == 0U)) continue;
        if (diagnostics->stop_priority && index >= 5U) continue;
        if (diagnostics->motion_priority && index >= 7U) continue;
        const PatrolSdoEntry* entry = &g_sdo_entries[index];
        if (!sdo_node_ready(diagnostics, entry->node, now_ms)) continue;
        uint8_t gated = (uint8_t)(index == 1U &&
            diagnostics->steering_tpdo_position_ms != 0U &&
            !fresh(1U, diagnostics->steering_tpdo_position_ms, now_ms,
                   PATROL_STEERING_TPDO_FRESH_MS));
        if (!gated && !due(now_ms, diagnostics->sdo_due_ms[index])) continue;
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

uint8_t PatrolDiagnostics_HasUrgentFeedback(const PatrolDiagnostics* diagnostics, uint32_t now_ms)
{
    if (diagnostics == 0) return 0U;
    if (diagnostics->pending_valid && diagnostics->pending_entry < 5U &&
        feedback_age(diagnostics, diagnostics->pending_entry, now_ms) >=
            PATROL_DIAG_FEEDBACK_PRIORITY_MS)
        return 1U;
    return (uint8_t)(urgent_feedback_entry(diagnostics, now_ms) != UINT8_MAX);
}

uint8_t PatrolDiagnostics_TakeSdoReadUrgent(PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                            PatrolCanFrame* frame, uint8_t allow_retry)
{
    uint8_t urgent;
    if (diagnostics == 0 || frame == 0) return 0U;
    urgent = urgent_feedback_entry(diagnostics, now_ms);
    if (diagnostics->pending_valid) {
        const PatrolSdoEntry* entry = &g_sdo_entries[diagnostics->pending_entry];
        if (!sdo_node_ready(diagnostics, entry->node, now_ms)) {
            finish_pending(diagnostics, now_ms);
        } else {
            if ((now_ms - diagnostics->pending_sent_ms) < PATROL_DIAG_SDO_TIMEOUT_MS) return 0U;
            diagnostics->sdo_timeout_count++;
            if (diagnostics->pending_entry < 5U &&
                diagnostics->pending_retry < PATROL_DIAG_SDO_RETRY_COUNT &&
                allow_retry && retry_feedback_ready(diagnostics, now_ms)) {
                diagnostics->pending_retry++;
                diagnostics->pending_sent_ms = now_ms;
                *frame = PatrolCanopen_SdoRead(entry->node, entry->index, entry->subindex);
                return 1U;
            }
            finish_pending(diagnostics, now_ms);
        }
        urgent = urgent_feedback_entry(diagnostics, now_ms);
    }
    return urgent == UINT8_MAX ? 0U : take_entry(diagnostics, urgent, now_ms, frame);
}

// 记录诊断读请求未能提交到 CAN。清理在途事务并维护发送失败统计，允许后续按调度重试。
void PatrolDiagnostics_OnSdoTxError(PatrolDiagnostics* diagnostics, uint32_t now_ms)
{
    if (diagnostics == 0) return;
    diagnostics->sdo_tx_error_count++;
    if (diagnostics->pending_valid) diagnostics->pending_sent_ms = now_ms - PATROL_DIAG_SDO_TIMEOUT_MS;
}

// 登记编码器规格寄存器与成功读取时间。用于区分编码器在线、规格合法和正式标定兼容，不能只凭位置计数认定配置正确。
void PatrolDiagnostics_OnEncoderConfig(PatrolDiagnostics* diagnostics, const uint16_t registers[3],
                                       uint32_t now_ms)
{
    if (diagnostics == 0 || registers == 0) return;
    memcpy(diagnostics->encoder_config, registers, sizeof(diagnostics->encoder_config));
    diagnostics->encoder_config_valid = 1U;
    diagnostics->encoder_config_ms = now_ms;
}

// 更新编码器原始计数及成功采样时间。只有有效解析后的计数应调用本接口，以保证时效门禁可信。
void PatrolDiagnostics_OnEncoderCount(PatrolDiagnostics* diagnostics, uint32_t count,
                                      uint32_t now_ms)
{
    if (diagnostics == 0) return;
    diagnostics->encoder_count = count;
    diagnostics->encoder_ms = now_ms;
    diagnostics->encoder_seen = 1U;
}

// 累计编码器错误并保存最近错误类别。与成功采样时间独立维护，错误不能延长旧样本的可用时间。
void PatrolDiagnostics_OnEncoderError(PatrolDiagnostics* diagnostics, PatrolEncoderError error)
{
    if (diagnostics == 0) return;
    diagnostics->last_encoder_error = (uint8_t)error;
    if (error == PATROL_ENCODER_ERROR_TIMEOUT) diagnostics->encoder_timeout_count++;
    else if (error == PATROL_ENCODER_ERROR_CRC) diagnostics->encoder_crc_error_count++;
    else if (error == PATROL_ENCODER_ERROR_EXCEPTION) diagnostics->encoder_exception_count++;
    else if (error == PATROL_ENCODER_ERROR_FORMAT) diagnostics->encoder_format_error_count++;
    else diagnostics->encoder_transport_error_count++;
}

// 生成指定时刻的一致诊断快照。计算节点在线、各反馈年龄与规格有效性，供运动门禁和查询使用；调用者负责共享缓存的并发保护。
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
        diagnostics->drive_status_word) & 0x0008U) != 0U || diagnostics->drive_objects[0] != 0 ||
        diagnostics->drive_objects[1] != 0);
    snapshot->steering_heartbeat_state = diagnostics->steering_heartbeat_state;
    snapshot->drive_heartbeat_state = diagnostics->drive_heartbeat_state;
    snapshot->steering_status_word = diagnostics->steering_status_word;
    snapshot->drive_status_word = diagnostics->drive_status_word;
    snapshot->steering_status_fresh = fresh(diagnostics->steering_status_word != 0,
        diagnostics->steering_status_ms, now_ms, PATROL_DIAG_FEEDBACK_TIMEOUT_MS);
    snapshot->drive_status_fresh = fresh(diagnostics->drive_status_word != 0,
        diagnostics->drive_status_ms, now_ms, PATROL_DIAG_FEEDBACK_TIMEOUT_MS);
    snapshot->steering_position_count = diagnostics->steering_position_count;
    snapshot->left_actual_rpm = diagnostics->left_actual_rpm;
    snapshot->right_actual_rpm = diagnostics->right_actual_rpm;
    snapshot->encoder_count = diagnostics->encoder_count;
    snapshot->encoder_sample_ms = diagnostics->encoder_ms;
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
    snapshot->encoder_format_error_count = diagnostics->encoder_format_error_count;
    snapshot->encoder_transport_error_count = diagnostics->encoder_transport_error_count;
    snapshot->last_encoder_error = diagnostics->last_encoder_error;
}

// 把 CAN 诊断快照格式化为受限 JSON。包含总线状态、原始轮速、错误计数和反馈门禁；输出受 size 限制，格式化成功不代表物理动作成功。
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
        "\"driveObjects\":[%ld,%ld,%ld,%ld,%ld,%ld,%ld],"
        "\"encoderRaw\":%lu,\"encoderOnline\":%s,\"encoderConfigValid\":%s,"
        "\"encoderLastError\":%u,\"encoderTimeouts\":%lu,\"encoderCrcErrors\":%lu,"
        "\"encoderExceptions\":%lu,\"encoderFormatErrors\":%lu,\"encoderTransportErrors\":%lu}",
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
        (long)s->drive_objects[6], (unsigned long)s->encoder_count,
        s->encoder_online ? "true" : "false", s->encoder_config_valid ? "true" : "false",
        (unsigned int)s->last_encoder_error, (unsigned long)s->encoder_timeout_count,
        (unsigned long)s->encoder_crc_error_count, (unsigned long)s->encoder_exception_count,
        (unsigned long)s->encoder_format_error_count,
        (unsigned long)s->encoder_transport_error_count);
    return (uint8_t)(length >= 0 && length < size);
}
