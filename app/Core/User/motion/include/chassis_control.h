#ifndef CHASSIS_CONTROL_H
#define CHASSIS_CONTROL_H

#include <stdint.h>

#define CHASSIS_MAX_RPM 200
#define CHASSIS_MAX_STEERING_MDEG 30000
#define CHASSIS_MAX_LEASE_MS 1000U
#define CHASSIS_FEEDBACK_TIMEOUT_MS 250U

typedef enum {
    CHASSIS_STOP_NONE = 0,
    CHASSIS_STOP_REMOTE_DISABLED,
    CHASSIS_STOP_UNCALIBRATED,
    CHASSIS_STOP_FEEDBACK_STALE,
    CHASSIS_STOP_ANGLE_MISMATCH,
    CHASSIS_STOP_NODE_OFFLINE,
    CHASSIS_STOP_CAN_BUS_OFF,
    CHASSIS_STOP_DRIVE_FAULT,
    CHASSIS_STOP_OTA_BUSY,
    CHASSIS_STOP_POWER_NOT_READY,
    CHASSIS_STOP_LEASE_EXPIRED,
    CHASSIS_STOP_DIRECTION_TIMEOUT,
    CHASSIS_STOP_EXPLICIT,
    CHASSIS_STOP_EXECUTION_FAULT,
    CHASSIS_STOP_ENCODER_RANGE,
    CHASSIS_STOP_ENCODER_PROFILE
} ChassisStopReason;

typedef struct {
    uint8_t calibrated;
    uint8_t steering_online;
    uint8_t drive_online;
    uint8_t can_bus_off;
    uint8_t drive_fault;
    uint8_t ota_busy;
    uint8_t power_ready;
    uint8_t encoder_range_valid;
    uint8_t encoder_profile_ready;
    int16_t left_actual_rpm;
    int16_t right_actual_rpm;
    int32_t steering_actual_mdeg;
    int32_t encoder_actual_mdeg;
    uint32_t feedback_ms;
} ChassisFeedback;

typedef struct {
    char session_id[40];
    uint32_t last_seq;
    uint32_t lease_deadline_ms;
    uint32_t reversal_started_ms;
    uint32_t last_zero_feedback_ms;
    int16_t requested_rpm;
    int16_t target_rpm;
    int32_t target_steering_mdeg;
    int8_t active_direction;
    uint8_t remote_enabled;
    uint8_t session_active;
    uint8_t reversal_pending;
    uint8_t zero_confirmations;
    uint8_t enable_request;
    uint8_t fault_latched;
    ChassisStopReason stop_reason;
    ChassisFeedback feedback;
} ChassisControl;

void ChassisControl_Init(ChassisControl* control, uint8_t remote_enabled);
void ChassisControl_SetFeedback(ChassisControl* control, const ChassisFeedback* feedback);
uint8_t ChassisControl_Apply(ChassisControl* control, const char* action, const char* session_id,
                             uint32_t seq, uint32_t lease_ms, int16_t drive_rpm,
                             int32_t steering_mdeg, uint32_t now_ms);
void ChassisControl_Tick(ChassisControl* control, uint32_t now_ms);
uint8_t ChassisControl_SafetyReady(const ChassisControl* control, uint32_t now_ms);

#endif
