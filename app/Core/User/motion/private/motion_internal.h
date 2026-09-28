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

    CenterCalibration center_cal;
    uint32_t center_cal_serial;
} PatrolMotion;

extern PatrolMotion g_motion;

uint32_t CenterCalibration_GateMask(const PatrolDiagnosticsSnapshot* s,
                                 uint8_t require_steering_disabled);
void CenterCalibration_UpdateRelative(const PatrolDiagnosticsSnapshot* s);
void CenterCalibration_CaptureFailureLocked(CenterCalibrationReason reason, uint32_t gate_mask,
                                          uint32_t now,
                                          const PatrolDiagnosticsSnapshot* snapshot);
void CenterCalibration_RequestStopForFault(CenterCalibrationReason reason, uint32_t gate_mask,
                                          uint32_t now);
void CenterCalibration_Fail(uint32_t fault, CenterCalibrationReason reason,
                                    const PatrolDiagnosticsSnapshot* snapshot,
                                    uint32_t gate_mask);

/* Integration helpers. ApplyReset and Take require the runtime lock;
 * ApplyCut and UpdateFeedback retain their own task/critical-section boundaries. */
void PatrolRuntime_ApplyReset(uint32_t now);
uint32_t PatrolRuntime_ApplyCut(uint32_t now);
void PatrolRuntime_UpdateFeedback(uint32_t now);
uint8_t PatrolRuntime_Take(uint32_t now, PatrolCanFrame* request, uint8_t* owner);
#endif
