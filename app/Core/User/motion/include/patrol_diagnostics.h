#ifndef PATROL_CHASSIS_DIAGNOSTICS_H
#define PATROL_CHASSIS_DIAGNOSTICS_H

#include <stdint.h>

#include "patrol_canopen.h"

#define PATROL_DIAG_SDO_ENTRY_COUNT 12U
#define PATROL_DIAG_ENCODER_CONFIG_REGS 3U
#define PATROL_DIAG_HEARTBEAT_TIMEOUT_MS 3000U
#define PATROL_DIAG_FEEDBACK_TIMEOUT_MS 250U
/* 在反馈真正过期前预留一个 SDO 超时窗口，避免运动事务长期占用总线。 */
#define PATROL_DIAG_FEEDBACK_PRIORITY_MS 150U

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
    uint32_t steering_tpdo_position_ms;
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
    uint32_t drive_code_ms[2];
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
    uint8_t stop_priority;
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
    uint32_t drive_code_ms[2];
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

// 初始化只读运动诊断状态与轮询进度。清空在线、反馈、错误和在途事务，不发送写对象请求。
void PatrolDiagnostics_Init(PatrolDiagnostics* diagnostics);
// 解析已核验的 Node 1 TPDO：0x181 持续提供状态字，0x281 持续提供转向位置。返回非零表示该帧已作为实时反馈消费。
uint8_t PatrolDiagnostics_OnTpdoFrame(PatrolDiagnostics* diagnostics,
                                      const PatrolCanFrame* frame, uint32_t now_ms);
// 解析节点心跳、PDO 和匹配的诊断 SDO 响应。根据有效报文更新对应缓存与时间戳，保留 Abort 和协议错误证据，不把无关帧算作目标对象响应。
void PatrolDiagnostics_OnCanFrame(PatrolDiagnostics* diagnostics, const PatrolCanFrame* frame,
                                  uint32_t now_ms);
// 取出下一条只读 SDO 请求。allow_retry 必须由调用方按当前安全门禁给出；超时只计数，反馈有足够时效余量且无其他故障时至多重读一次，不刷新旧样本或自行锁存。
uint8_t PatrolDiagnostics_TakeSdoRead(PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                     PatrolCanFrame* frame, uint8_t allow_retry);
// 查询关键运动反馈是否已接近过期，调用方可据此拒绝执行器继续抢占 CAN1 发送机会。
uint8_t PatrolDiagnostics_HasUrgentFeedback(const PatrolDiagnostics* diagnostics, uint32_t now_ms);
// 优先取得一条临近过期的关键反馈读取；不改变 250 ms 安全时效，只提高读取调度优先级。
uint8_t PatrolDiagnostics_TakeSdoReadUrgent(PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                            PatrolCanFrame* frame, uint8_t allow_retry);
// 记录诊断读请求未能提交到 CAN。清理在途事务并维护发送失败统计，允许后续按调度重试。
void PatrolDiagnostics_OnSdoTxError(PatrolDiagnostics* diagnostics, uint32_t now_ms);
// 登记编码器规格寄存器与成功读取时间。用于区分编码器在线、规格合法和正式标定兼容，不能只凭位置计数认定配置正确。
void PatrolDiagnostics_OnEncoderConfig(PatrolDiagnostics* diagnostics, const uint16_t registers[3],
                                       uint32_t now_ms);
// 更新编码器原始计数及成功采样时间。只有有效解析后的计数应调用本接口，以保证时效门禁可信。
void PatrolDiagnostics_OnEncoderCount(PatrolDiagnostics* diagnostics, uint32_t count,
                                      uint32_t now_ms);
// 累计编码器错误并保存最近错误类别。与成功采样时间独立维护，错误不能延长旧样本的可用时间。
void PatrolDiagnostics_OnEncoderError(PatrolDiagnostics* diagnostics, PatrolEncoderError error);
// 生成指定时刻的一致诊断快照。计算节点在线、各反馈年龄与规格有效性，供运动门禁和查询使用；调用者负责共享缓存的并发保护。
void PatrolDiagnostics_Snapshot(const PatrolDiagnostics* diagnostics, uint32_t now_ms,
                                PatrolDiagnosticsSnapshot* snapshot);
// 把 CAN 诊断快照格式化为受限 JSON。包含总线状态、原始轮速、错误计数和反馈门禁；输出受 size 限制，格式化成功不代表物理动作成功。
uint8_t PatrolDiagnostics_FormatCanJson(const PatrolDiagnosticsSnapshot* snapshot,
    uint8_t bus_off, uint8_t bus_off_active, uint32_t rx_overflow,
    uint32_t can1_raw_rx, uint32_t can2_raw_rx, char* output, uint16_t size);

#endif
