#ifndef PATROL_MOTION_INTERNAL_H
#define PATROL_MOTION_INTERNAL_H

/* Private to the motion runtime and calibration module. All cross-task access
 * retains the original critical sections through PatrolRuntimePort. */
#include "patrol_runtime.h"
#include "center_calibration.h"
#include "patrol_runtime_port.h"
#include "bench_steering.h"
#include "bench_drive.h"
#include "steering_angle.h"
#include "patrol_auto_center.h"

typedef enum {
    CENTER_CAL_IDLE = 0,
    CENTER_CAL_READY,
    CENTER_CAL_JOGGING,
    CENTER_CAL_STOPPING,
    CENTER_CAL_COMMIT_STABLE,
    CENTER_CAL_ENCODER_WRITE,
    CENTER_CAL_ENCODER_VERIFY,
    CENTER_CAL_COMPLETE,
    CENTER_CAL_FAILED,
    CENTER_CAL_START_STABLE,
    CENTER_CAL_EXPIRED
} CenterCalibrationPhase;

typedef enum {
    CENTER_CAL_REASON_NONE = 0,
    CENTER_CAL_REASON_READY_LEASE_TIMEOUT,
    CENTER_CAL_REASON_PHASE_TIMEOUT,
    CENTER_CAL_REASON_GATE_NOT_READY,
    CENTER_CAL_REASON_RELATIVE_MISMATCH,
    CENTER_CAL_REASON_BENCH_EXECUTOR_FAULT,
    CENTER_CAL_REASON_COMMIT_BENCH_BUSY,
    CENTER_CAL_REASON_COMMIT_STEERING_ENABLED,
    CENTER_CAL_REASON_CONFIG_CLEAR_FAILED,
    CENTER_CAL_REASON_ENCODER_TX_FAILED,
    CENTER_CAL_REASON_ENCODER_VERIFY_MISMATCH,
    CENTER_CAL_REASON_CONFIG_SAVE_FAILED,
    CENTER_CAL_REASON_WRONG_PHASE,
    CENTER_CAL_REASON_SESSION_MISMATCH,
    CENTER_CAL_REASON_INVALID_JOG_DELTA,
    CENTER_CAL_REASON_JOG_LIMIT,
    CENTER_CAL_REASON_ALREADY_ACTIVE,
    CENTER_CAL_REASON_ENCODER_OUT_OF_RANGE,
    CENTER_CAL_REASON_EXECUTOR_BUSY,
    CENTER_CAL_REASON_POSITION_EPOCH_RESET
} CenterCalibrationReason;

#define CENTER_GATE_POWER_OFF                  (1UL << 0)
#define CENTER_GATE_CAN_BUS_OFF                (1UL << 1)
#define CENTER_GATE_CAN_BUS_OFF_ACTIVE         (1UL << 2)
#define CENTER_GATE_OTA_BUSY                   (1UL << 3)
#define CENTER_GATE_STEERING_OFFLINE           (1UL << 4)
#define CENTER_GATE_DRIVE_OFFLINE              (1UL << 5)
#define CENTER_GATE_STEERING_FEEDBACK_STALE    (1UL << 6)
#define CENTER_GATE_DRIVE_FEEDBACK_STALE       (1UL << 7)
#define CENTER_GATE_STEERING_STATUS_STALE      (1UL << 8)
#define CENTER_GATE_DRIVE_STATUS_STALE         (1UL << 9)
#define CENTER_GATE_ENCODER_OFFLINE            (1UL << 10)
#define CENTER_GATE_ENCODER_CONFIG_INVALID     (1UL << 11)
#define CENTER_GATE_DRIVE_FAULT                (1UL << 12)
#define CENTER_GATE_WHEELS_MOVING              (1UL << 13)
#define CENTER_GATE_CONTROL_SESSION_ACTIVE     (1UL << 15)
#define CENTER_GATE_CONTROL_ENABLE_ACTIVE      (1UL << 16)
#define CENTER_GATE_DRIVE_OPERATION_ENABLED    (1UL << 17)
#define CENTER_GATE_FAULT_LATCHED              (1UL << 18)
#define CENTER_GATE_STEERING_OPERATION_ENABLED (1UL << 19)
#define CENTER_GATE_ENCODER_OUT_OF_RANGE       (1UL << 20)
#define CENTER_GATE_BENCH_EXECUTOR_BUSY        (1UL << 21)
#define CENTER_GATE_DRIVE_EXECUTOR_BUSY        (1UL << 22)
#define CENTER_GATE_RUNTIME_UNSAFE             (1UL << 23)
#define CENTER_GATE_ACTUATION_DISABLED         (1UL << 24)

typedef struct {
    uint32_t at_ms;
    uint32_t gate_mask;
    uint32_t bench_fault;
    uint32_t last_sdo_abort;
    int32_t driver_raw;
    uint32_t encoder_raw;
    int32_t left_rpm;
    int32_t right_rpm;
    uint32_t feedback_age_ms;
    uint32_t encoder_age_ms;
    uint32_t encoder_error_total;
    uint8_t phase;
    uint8_t reason;
    uint8_t bench_phase;
    uint8_t bench_step;
    uint8_t encoder_write_state;
    uint8_t encoder_error;
    uint16_t steering_status_word;
    uint16_t drive_status_word;
} CenterCalibrationFailure;

typedef struct {
    CenterCalibrationPhase phase;
    char session_id[40];
    uint32_t serial;
    uint32_t deadline_ms;
    uint32_t last_sample_ms;
    uint32_t fault;
    int32_t start_mdeg;
    int32_t target_mdeg;
    int32_t accumulated_mdeg;
    int32_t captured_driver_count;
    int32_t start_driver_count;
    int32_t encoder_delta_count;
    int32_t driver_relative_mdeg;
    int32_t encoder_relative_mdeg;
    uint32_t last_encoder_count;
    uint32_t last_encoder_sample_ms;
    uint8_t stable_samples;
    uint8_t relative_valid;
    uint8_t cancel_requested;
    uint8_t stop_fault_pending;
    uint8_t stop_fault_reason;
    uint32_t stop_gate_mask;
    volatile uint8_t encoder_write_state; /* 0 idle, 1 requested, 2 echoed, 3 not sent, 4 executing, 5 uncertain. */
    CenterCalibrationFailure failure;
    uint32_t last_reject_ms;
    uint32_t last_reject_gate_mask;
    uint8_t last_reject_reason;
} CenterCalibration;

typedef struct {
    PatrolDiagnostics diagnostics;
    BenchSteering bench;
    BenchDrive drive;
    uint8_t drive_turn;
    uint8_t bench_turn;
    uint8_t bench_transport_stopped;
    uint16_t feedback_gate_mask, bench_fault_gate_mask;
    SteeringCoordinate coordinate;
    uint8_t coordinate_reset_requested;
    int32_t coordinate_calibration_center;
    uint32_t coordinate_absolute_center;
    PatrolAutoCenter auto_center;

    CenterCalibration center_cal;
    uint32_t center_cal_serial;
} PatrolMotion;

extern PatrolMotion g_motion;

// 汇总标定阶段所需的全部安全门禁位。检查供电、CAN、OTA、节点、双反馈、编码器、车轮运动、会话及执行器状态；require_steering_disabled 控制是否要求转向撤使能。
uint32_t CenterCalibration_GateMask(const PatrolDiagnosticsSnapshot* s,
                                 uint8_t require_steering_disabled);
// 从本轮起点计算驱动器和编码器相对角度。只在本次标定坐标成立时更新，并保留双传感器一致性判断所需的独立结果。
void CenterCalibration_UpdateRelative(const PatrolDiagnosticsSnapshot* s);
// 在标定失败瞬间冻结诊断证据。调用方持锁，记录时间、门禁、阶段、执行器、原始计数和错误；后续状态变化不应改写首因证据。
void CenterCalibration_CaptureFailureLocked(CenterCalibrationReason reason, uint32_t gate_mask,
                                          uint32_t now,
                                          const PatrolDiagnosticsSnapshot* snapshot);
// 在仍可发送停车报文时登记故障停车请求。保留标定执行器对 STOPPING 的归属，避免在未标定状态下误用普通运动门禁而跳过有序停机。
void CenterCalibration_RequestStopForFault(CenterCalibrationReason reason, uint32_t gate_mask,
                                          uint32_t now);
// 统一终止标定并关联执行器故障。保存失败快照并撤销后续标定动作，交由运行层安全停机；不把软件终止等同于物理停止。
void CenterCalibration_Fail(uint32_t fault, CenterCalibrationReason reason,
                                    const PatrolDiagnosticsSnapshot* snapshot,
                                    uint32_t gate_mask);

/* Integration helpers. ApplyReset and Take require the runtime lock;
 * ApplyCut and UpdateFeedback retain their own task/critical-section boundaries. */
// 处理下电或节点复位引起的转向坐标失效。下电清回中和限位对齐缓存，带电坐标周期丢失则停止转向并按标定阶段保留故障；调用方按运行层约定持锁。
void PatrolRuntime_ApplyReset(uint32_t now);
// 处理执行器请求的切电及故障传播。将执行错误汇总到控制层并更新动力输出命令；返回处理后的时间基准，输出写入成功不代表已获得接触器物理反馈。
uint32_t PatrolRuntime_ApplyCut(uint32_t now);
// 汇总驱动器与绝对编码器反馈并更新安全仲裁输入。同锁读取时间和诊断快照，建立运行坐标并归一化镜像右轮方向；标定相对反馈、Bus-Off 恢复观察和反馈门禁在此同步更新。
void PatrolRuntime_UpdateFeedback(uint32_t now);
// 在执行器和只读诊断之间仲裁下一个 CAN 请求。通过 owner 标识报文归属，维持运动期间事务优先级与交替读取；返回报文仅表示选中，真正发送前仍须复核授权。
uint8_t PatrolRuntime_Take(uint32_t now, PatrolCanFrame* request, uint8_t* owner);
#endif
