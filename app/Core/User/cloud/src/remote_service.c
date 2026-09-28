#include "remote_service.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "bench_drive.h"
#include "chassis_cloud_protocol.h"
#include "fixed_point.h"
#include "main.h"
#include "time_base.h"
#include "patrol_config.h"
#include "vehicle_power_output.h"
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

static uint8_t g_remote_initialized;
static ChassisControl g_control;
static char g_last_request_id[64];
static char g_last_response[512];
static uint32_t g_last_request_hash;
static uint16_t g_last_request_length;
static uint8_t g_bench_permit, g_bench_started, g_drive_permit;
static uint16_t g_bench_limit;
static uint32_t g_arm_deadline;
static char g_bench_session[40];
#if PATROL_CHASSIS_MQTT_LAB_TEST
static uint8_t g_lab_hold_state;
#endif

static uint32_t request_hash(const ChassisCloudCommand* command)
{
    uint32_t hash = 2166136261UL;
    char canonical[192];
    int length;
    int i;
    length = snprintf(canonical, sizeof(canonical), "%u|%s|%s|%lu|%lu|%d|%ld",
        (unsigned)command->type, command->action, command->session_id,
        (unsigned long)command->seq, (unsigned long)command->lease_ms,
        (int)command->drive_rpm, (long)command->steering_mdeg);
    if (length < 0 || length >= (int)sizeof(canonical)) return 0U;
    for (i = 0; i < length; i++) {
        hash ^= (uint8_t)canonical[i];
        hash *= 16777619UL;
    }
    return hash;
}

void RemoteService_BenchRevoke(void)
{
    taskENTER_CRITICAL();
    g_bench_permit = 0; g_drive_permit = 0; g_control.remote_enabled = 0;
    g_control.enable_request = 0; g_control.session_active = 0;
    g_control.target_rpm = 0; g_control.requested_rpm = 0;
    g_control.target_steering_mdeg = 0; g_control.session_id[0] = '\0';
    g_bench_session[0] = '\0';
    taskEXIT_CRITICAL();
}
void RemoteService_RevokeForNetworkChange(void)
{
    RemoteService_BenchRevoke();
    g_last_request_id[0] = '\0';
    g_last_response[0] = '\0';
    g_last_request_hash = 0U;
    g_last_request_length = 0U;
#if PATROL_CHASSIS_MQTT_LAB_TEST
    g_lab_hold_state = 0U;
#endif
    APP_LOGW("REMOTE", "MQTT runtime change revoked motion session and targets");
}
uint8_t RemoteService_FormatNetworkState(char *output, uint16_t size)
{
    int written;
    if (output == 0 || size == 0U) return 0U;
    written = snprintf(output, size,
        "{\"remotePermit\":%s,\"sessionActive\":%s,\"sessionId\":\"%s\","
        "\"enableRequest\":%s,\"targetRpm\":%d,\"requestedRpm\":%d,"
        "\"steeringTargetMdeg\":%ld,\"replayCached\":%s}",
        g_control.remote_enabled ? "true" : "false",
        g_control.session_active ? "true" : "false", g_control.session_id,
        g_control.enable_request ? "true" : "false", (int)g_control.target_rpm,
        (int)g_control.requested_rpm, (long)g_control.target_steering_mdeg,
        (g_last_request_id[0] != '\0' || g_last_response[0] != '\0' ||
         g_last_request_hash != 0U || g_last_request_length != 0U) ? "true" : "false");
    return written > 0 && written < size;
}
#if PATROL_CHASSIS_MQTT_LAB_TEST
void RemoteService_LabSeedNetworkState(void)
{
    taskENTER_CRITICAL();
    g_control.remote_enabled = 1U;
    g_control.session_active = 1U;
    g_control.enable_request = 1U;
    g_control.target_rpm = 37;
    g_control.requested_rpm = 41;
    g_control.target_steering_mdeg = 3200;
    strcpy(g_control.session_id, "mqtt-lab-old-session");
    strcpy(g_last_request_id, "mqtt-lab-replay");
    strcpy(g_last_response, "cached");
    g_last_request_hash = 0xA5A55A5AUL;
    g_last_request_length = 123U;
    g_lab_hold_state = 1U;
    taskEXIT_CRITICAL();
    APP_LOGW("REMOTE", "lab-only non-actuating network state seeded");
}
#endif
uint8_t RemoteService_BenchPermitted(void) { return g_bench_permit; }
void RemoteService_BenchFault(void)
{
    taskENTER_CRITICAL();
    RemoteService_BenchRevoke();
    if (!g_control.fault_latched) {
        g_control.stop_reason = CHASSIS_STOP_EXECUTION_FAULT;
    }
    g_control.fault_latched = 1;
    taskEXIT_CRITICAL();
}
uint16_t RemoteService_BenchLimit(void) { return g_bench_limit; }
const char* RemoteService_BenchSession(void) { return g_bench_session; }
uint8_t RemoteService_BenchArm(uint16_t limit_mdeg, const char* request_id, uint32_t now)
{
    uint8_t ready;
    if (!g_remote_initialized ||
        (limit_mdeg != 1000 && limit_mdeg != 3000 && limit_mdeg != 5000 &&
         limit_mdeg != 20000 && limit_mdeg != 30000) ||
        request_id == NULL || request_id[0] == '\0' ||
        strlen(request_id) > sizeof(g_bench_session) - 3U) return 0;
    taskENTER_CRITICAL();
    if (g_bench_permit || g_control.session_active) { taskEXIT_CRITICAL(); return 0; }
    g_control.remote_enabled = 1;
    ready = ChassisControl_SafetyReady(&g_control, now);
    if (ready) {
        g_bench_permit = 1; g_bench_started = 0; g_bench_limit = limit_mdeg;
        g_arm_deadline = now + 15000U;
        snprintf(g_bench_session, sizeof(g_bench_session), "b-%s", request_id);
        g_last_request_id[0] = 0; g_last_response[0] = 0;
    } else g_control.remote_enabled = 0;
    taskEXIT_CRITICAL();
    return ready;
}

uint8_t RemoteService_BenchDrivePermitted(void) { return g_drive_permit && g_bench_permit; }
uint8_t RemoteService_BenchDriveArm(uint16_t limit, const char* request_id, uint32_t now)
{
    uint8_t ready;
    taskENTER_CRITICAL();
    ready = RemoteService_BenchArm(limit, request_id, now);
    if (ready) g_drive_permit = 1;
    taskEXIT_CRITICAL();
    return ready;
}

void RemoteService_Init(void)
{
    const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
    ChassisFeedback feedback = {0};
    g_bench_permit = 0; g_bench_started = 0; g_drive_permit = 0;
    ChassisControl_Init(&g_control, 0U); /* A persisted flag never arms the bench. */
    feedback.calibrated = cfg != 0 && cfg->calibrated && PatrolConfig_CalibrationCompatible();
    feedback.feedback_ms = HAL_GetTick();
    ChassisControl_SetFeedback(&g_control, &feedback);
    g_last_request_id[0] = '\0';
    g_last_response[0] = '\0';
    g_last_request_hash = 0U;
    g_last_request_length = 0U;
    g_remote_initialized = 1U;
    APP_LOGI("REMOTE", "initialized enabled=%u calibrated=%u; outputs gated",
             (unsigned int)g_control.remote_enabled, (unsigned int)feedback.calibrated);
}

void RemoteService_Task(void)
{
    if (!g_remote_initialized) return;
#if PATROL_CHASSIS_MQTT_LAB_TEST
    if (g_lab_hold_state) return;
#endif
    taskENTER_CRITICAL();
    RemoteService_SafetyTick(HAL_GetTick());
    taskEXIT_CRITICAL();
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
    uint32_t payload_hash;
    uint8_t request_conflict = 0U;
    if (!g_remote_initialized || payload == 0 || response == 0 || size == 0U) return 0U;
    parse_status = ChassisCloud_Parse(payload, length, &command);
    if (command.request_id[0] == '\0') return 0U;
    payload_hash = request_hash(&command);
    if (strcmp(command.request_id, g_last_request_id) == 0 && g_last_response[0] != '\0')
    {
        if (payload_hash == g_last_request_hash) {
            if (strlen(g_last_response) >= size) return 0U;
            strcpy(response, g_last_response);
            return 1U;
        }
        request_conflict = 1U;
        parse_status = CHASSIS_CLOUD_REQUEST_ID_CONFLICT;
    }
    accepted = 0U;
    code = (uint16_t)parse_status;
    message = "invalid request";
    if (parse_status == CHASSIS_CLOUD_REQUEST_ID_CONFLICT) message = "requestId conflict";
    else if (parse_status == CHASSIS_CLOUD_UNSUPPORTED) message = "unsupported command or action";
    else if (parse_status == CHASSIS_CLOUD_OK)
    {
        uint8_t session_conflict = 0U;
        if (command.type == CHASSIS_CLOUD_COMMAND_POWER_ON ||
            command.type == CHASSIS_CLOUD_COMMAND_POWER_OFF)
        {
            uint8_t target_on = command.type == CHASSIS_CLOUD_COMMAND_POWER_ON ? 1U : 0U;
            if (!target_on) RemoteService_BenchRevoke();
            if (!VehiclePowerOutput_IsAvailable()) {
                code = 5001U;
                message = "power output unavailable";
            } else if (!VehiclePowerOutput_Set(target_on)) {
                code = 5001U;
                message = "power output write failed";
            } else {
                accepted = 1U;
                code = 200U;
                message = "operation accepted";
            }
            goto format_response;
        }
        if (strcmp(command.action, "stop") != 0 &&
            (!g_bench_permit || strcmp(command.session_id, g_bench_session) != 0 ||
             abs(command.drive_rpm) > (g_drive_permit ? BENCH_DRIVE_MAX_RPM : 0) ||
             labs(command.steering_mdeg) > g_bench_limit)) {
            code = 400U; message = "bench permit or motion limit rejected";
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
    if (command.type == CHASSIS_CLOUD_COMMAND_POWER_ON ||
        command.type == CHASSIS_CLOUD_COMMAND_POWER_OFF)
        written = snprintf(response, size,
            "{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{"
            "\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"result\":{\"powerOutputOn\":%s}}}",
            timestamp, command.request_id, (unsigned int)code, message,
            VehiclePowerOutput_IsOn() ? "true" : "false");
    else
        written = snprintf(response, size,
            "{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{"
            "\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"result\":{\"targetDriveRpm\":%d,"
            "\"targetSteeringDeg\":%s,\"stopReason\":%u}}}",
            timestamp, command.request_id, (unsigned int)code, message,
            (int)g_control.target_rpm, steering_value, (unsigned int)g_control.stop_reason);
    if (written <= 0 || written >= size) return 0U;
    if (!request_conflict) {
        strncpy(g_last_request_id, command.request_id, sizeof(g_last_request_id) - 1U);
        g_last_request_id[sizeof(g_last_request_id) - 1U] = '\0';
        g_last_request_hash = payload_hash;
        g_last_request_length = length;
        if ((size_t)written < sizeof(g_last_response)) strcpy(g_last_response, response);
    }
    return 1U;
}

const ChassisControl* RemoteService_State(void) { return &g_control; }
