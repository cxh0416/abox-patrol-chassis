#include "remote_service.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "chassis_cloud_protocol.h"
#include "fixed_point.h"
#include "main.h"
#include "time_base.h"
#include "vcu_config.h"
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

static uint8_t g_remote_initialized;
static ChassisControl g_control;
static char g_last_request_id[64];
static char g_last_response[512];
static uint8_t g_bench_permit, g_bench_started;
static uint16_t g_bench_limit;
static uint32_t g_arm_deadline, g_arm_serial;
static char g_bench_session[40];

void RemoteService_BenchRevoke(void)
{
    taskENTER_CRITICAL();
    g_bench_permit = 0; g_control.remote_enabled = 0;
    g_control.enable_request = 0; g_control.session_active = 0;
    g_control.target_rpm = 0; g_control.requested_rpm = 0;
    taskEXIT_CRITICAL();
}
uint8_t RemoteService_BenchPermitted(void) { return g_bench_permit; }
uint16_t RemoteService_BenchLimit(void) { return g_bench_limit; }
const char* RemoteService_BenchSession(void) { return g_bench_session; }
uint8_t RemoteService_BenchArm(uint16_t limit_mdeg, uint32_t now)
{
    char stamp[21];
    uint8_t ready;
    if (!g_remote_initialized || (limit_mdeg != 1000 && limit_mdeg != 3000 && limit_mdeg != 5000) ||
        !TimeBase_FormatUtcMs(stamp, sizeof(stamp))) return 0;
    taskENTER_CRITICAL();
    if (g_bench_permit || g_control.session_active) { taskEXIT_CRITICAL(); return 0; }
    g_control.remote_enabled = 1;
    ready = ChassisControl_SafetyReady(&g_control, now);
    if (ready) {
        g_bench_permit = 1; g_bench_started = 0; g_bench_limit = limit_mdeg;
        g_arm_deadline = now + 15000U;
        snprintf(g_bench_session, sizeof(g_bench_session), "b-%s-%lu", stamp, (unsigned long)++g_arm_serial);
        g_last_request_id[0] = 0; g_last_response[0] = 0;
    } else g_control.remote_enabled = 0;
    taskEXIT_CRITICAL();
    return ready;
}

void RemoteService_Init(void)
{
    const VcuRemoteConfig_t* cfg = VcuConfig_Get();
    ChassisFeedback feedback = {0};
    g_bench_permit = 0; g_bench_started = 0;
    ChassisControl_Init(&g_control, 0U); /* A persisted flag never arms the bench. */
    feedback.calibrated = cfg != 0 && cfg->calibrated && VcuConfig_CalibrationCompatible();
    feedback.feedback_ms = HAL_GetTick();
    ChassisControl_SetFeedback(&g_control, &feedback);
    g_last_request_id[0] = '\0';
    g_last_response[0] = '\0';
    g_remote_initialized = 1U;
    APP_LOGI("REMOTE", "initialized enabled=%u calibrated=%u; outputs gated",
             (unsigned int)g_control.remote_enabled, (unsigned int)feedback.calibrated);
}

void RemoteService_Task(void)
{
    if (!g_remote_initialized) return;
    RemoteService_SafetyTick(HAL_GetTick());
}

void RemoteService_SafetyTick(uint32_t now_ms)
{
    if (!g_remote_initialized) return;
    taskENTER_CRITICAL();
    ChassisControl_Tick(&g_control, now_ms);
    if (g_bench_permit && (!ChassisControl_SafetyReady(&g_control, now_ms) ||
        (g_bench_started && (!g_control.session_active || !g_control.enable_request)) ||
        (!g_bench_started && (int32_t)(now_ms - g_arm_deadline) >= 0))) RemoteService_BenchRevoke();
    taskEXIT_CRITICAL();
}

void RemoteService_UpdateFeedback(const ChassisFeedback* feedback)
{
    if (g_remote_initialized && feedback != 0) ChassisControl_SetFeedback(&g_control, feedback);
}

uint8_t RemoteService_HandleControl(const char* payload, uint16_t length, char* response, uint16_t size)
{
    ChassisCloudCommand command;
    ChassisCloudStatus parse_status;
    uint8_t accepted;
    uint16_t code;
    const char* message;
    char timestamp[21];
    char steering_value[24];
    int written;
    if (!g_remote_initialized || response == 0 || size == 0U) return 0U;
    parse_status = ChassisCloud_Parse(payload, length, &command);
    if (command.request_id[0] == '\0') return 0U;
    if (strcmp(command.request_id, g_last_request_id) == 0 && g_last_response[0] != '\0')
    {
        if (strlen(g_last_response) >= size) return 0U;
        strcpy(response, g_last_response);
        return 1U;
    }
    accepted = 0U;
    code = (uint16_t)parse_status;
    message = "invalid request";
    if (parse_status == CHASSIS_CLOUD_DEVICE_TYPE_MISMATCH) message = "device type mismatch";
    else if (parse_status == CHASSIS_CLOUD_UNSUPPORTED) message = "unsupported command or action";
    else if (parse_status == CHASSIS_CLOUD_OK)
    {
        uint8_t session_conflict = 0U;
        if (strcmp(command.action, "stop") != 0 &&
            (!g_bench_permit || strcmp(command.session_id, g_bench_session) != 0 ||
             command.drive_rpm != 0 || labs(command.steering_mdeg) > g_bench_limit)) {
            code = 400U; message = "bench permit or steering-only limit rejected";
            goto format_response;
        }
        if (strcmp(command.action, "renew") == 0)
            session_conflict = (uint8_t)(!g_control.session_active ||
                strcmp(g_control.session_id, command.session_id) != 0);
        else if (g_control.session_active && strcmp(g_control.session_id, command.session_id) != 0)
            session_conflict = 1U;
        if (session_conflict)
        {
            code = 4003U;
            message = "control session busy";
        }
        else if (strcmp(command.action, "stop") != 0 && g_control.session_active &&
                 command.seq <= g_control.last_seq)
        {
            code = 4001U;
            message = "sequence is not increasing";
        }
        else if (strcmp(command.action, "stop") != 0 &&
                 !ChassisControl_SafetyReady(&g_control, HAL_GetTick()))
        {
            code = 400U;
            message = "motion safety gate rejected";
        }
        else
        {
            taskENTER_CRITICAL();
            accepted = ChassisControl_Apply(&g_control, command.action, command.session_id,
                                            command.seq, command.lease_ms, command.drive_rpm,
                                            command.steering_mdeg, HAL_GetTick());
            if (accepted) {
                if (strcmp(command.action, "stop") == 0) RemoteService_BenchRevoke();
                else g_bench_started = 1;
            }
            taskEXIT_CRITICAL();
            code = accepted ? 200U : 5001U;
            message = accepted ? "operation accepted" : "internal control error";
        }
    }
format_response:
    if (!TimeBase_FormatUtcMs(timestamp, sizeof(timestamp))) strcpy(timestamp, "0");
    if (!FixedPoint_FormatMilli(steering_value, sizeof(steering_value),
                                g_control.target_steering_mdeg)) return 0U;
    written = snprintf(response, size,
        "{\"version\":\"3.0\",\"timestamp\":%s,\"data\":{\"deviceType\":\"%s\","
        "\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"targetDriveRpm\":%d,"
        "\"targetSteeringDeg\":%s,\"stopReason\":%u}}",
        timestamp, CHASSIS_PUBLIC_DEVICE_TYPE, command.request_id, (unsigned int)code, message,
        (int)g_control.target_rpm, steering_value, (unsigned int)g_control.stop_reason);
    if (written <= 0 || written >= size) return 0U;
    strncpy(g_last_request_id, command.request_id, sizeof(g_last_request_id) - 1U);
    g_last_request_id[sizeof(g_last_request_id) - 1U] = '\0';
    if ((size_t)written < sizeof(g_last_response)) strcpy(g_last_response, response);
    return 1U;
}

const ChassisControl* RemoteService_State(void) { return &g_control; }
