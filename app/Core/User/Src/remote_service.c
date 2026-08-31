#include "remote_service.h"

#include <stdio.h>

#include "app_log.h"
#include "chassis_cloud_protocol.h"
#include "main.h"
#include "vcu_config.h"

static uint8_t g_remote_initialized;
static ChassisControl g_control;

void RemoteService_Init(void)
{
    const VcuRemoteConfig_t* cfg = VcuConfig_Get();
    ChassisFeedback feedback = {0};
    ChassisControl_Init(&g_control, cfg != 0 ? cfg->remote_enabled : 0U);
    feedback.calibrated = cfg != 0 ? cfg->calibrated : 0U;
    feedback.feedback_ms = HAL_GetTick();
    ChassisControl_SetFeedback(&g_control, &feedback);
    g_remote_initialized = 1U;
    APP_LOGI("REMOTE", "initialized enabled=%u calibrated=%u; outputs gated",
             (unsigned int)g_control.remote_enabled, (unsigned int)feedback.calibrated);
}

void RemoteService_Task(void)
{
    if (!g_remote_initialized) return;
    ChassisControl_Tick(&g_control, HAL_GetTick());
}

void RemoteService_SafetyTick(uint32_t now_ms)
{
    if (g_remote_initialized) ChassisControl_Tick(&g_control, now_ms);
}

uint8_t RemoteService_HandleControl(const char* payload, uint16_t length, char* response, uint16_t size)
{
    ChassisCloudCommand command;
    uint8_t accepted;
    int written;
    if (!g_remote_initialized || response == 0 || size == 0U ||
        !ChassisCloud_Parse(payload, length, &command)) return 0U;
    accepted = ChassisControl_Apply(&g_control, command.action, command.session_id, command.seq,
                                    command.lease_ms, command.drive_rpm,
                                    command.steering_mdeg, HAL_GetTick());
    written = snprintf(response, size,
        "{\"version\":\"3.0\",\"data\":{\"requestId\":\"%s\",\"deviceType\":\"patrol_vehicle_chassis\","
        "\"commandAccepted\":%s,\"vehicleMoving\":false,\"targetDriveRpm\":%d,"
        "\"targetSteeringDeg\":%.3f,\"stopReason\":%u}}",
        command.request_id, accepted ? "true" : "false", (int)g_control.target_rpm,
        (double)g_control.target_steering_mdeg / 1000.0, (unsigned int)g_control.stop_reason);
    return (written > 0 && written < size) ? 1U : 0U;
}

const ChassisControl* RemoteService_State(void) { return &g_control; }
