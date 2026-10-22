#include "remote_service.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "chassis_cloud_protocol.h"
#include "fixed_point.h"
#include "main.h"
#include "time_base.h"
#include "abox_mqtt_v4.h"
#include "abox_mqtt_digest.h"
#include "patrol_config.h"
#include "vehicle_power_output.h"
#include "center_calibration.h"
#include "patrol_runtime.h"
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

/* 私有 RAM 缓存按 4 字节对齐，去除每槽尾部 4 字节填充；64 位时间、4 槽及 64 字符 requestId 均保留。 */
typedef struct __attribute__((packed, aligned(4))) {
    uint64_t expires_at, protect_until, protect_mono;
    uint16_t code;
    uint8_t power_on;
    char request_id[65];
} ABoxPowerReplay;
_Static_assert(sizeof(ABoxPowerReplay) == 92U, "Power replay RAM budget changed");
static ABoxPowerReplay g_power_replay[4];

static uint8_t g_remote_initialized;
static ChassisControl g_control;
static char g_last_request_id[65];
/* Covers the longest motion result/message with a 64-byte ID and UTC integer. */
static char g_last_response[288];
static uint8_t g_last_digest[32];
#if PATROL_CHASSIS_MQTT_LAB_TEST
static uint8_t g_lab_hold_state;
#endif


// 在临界区撤销共享控制会话和运动目标。清除使能、会话标识、行走和转向目标；只操作控制状态，驱动器停车由运动任务推进。
void RemoteService_Revoke(void)
{
    taskENTER_CRITICAL();
    g_control.enable_request = 0; g_control.session_active = 0;
    g_control.target_rpm = 0; g_control.requested_rpm = 0;
    g_control.target_steering_mdeg = 0; g_control.session_id[0] = '\0';
    taskEXIT_CRITICAL();
}
// 在网络身份或 MQTT 运行配置变化前撤销控制权。同时清空请求重放缓存，防止旧连接的授权和缓存结果跨网络边界继续生效。
void RemoteService_RevokeForNetworkChange(void)
{
    RemoteService_Revoke();
    g_last_request_id[0] = '\0';
    g_last_response[0] = '\0';
#if PATROL_CHASSIS_MQTT_LAB_TEST
    g_lab_hold_state = 0U;
#endif
    APP_LOGW("REMOTE", "MQTT runtime change revoked motion session and targets");
}
// 格式化用于诊断网络切换安全性的控制状态。输出会话、目标、使能与重放缓存标志，检查 snprintf 截断；不改变控制状态。
uint8_t RemoteService_FormatNetworkState(char *output, uint16_t size)
{
    int written;
    if (output == 0 || size == 0U) return 0U;
    written = snprintf(output, size,
        "{\"sessionActive\":%s,\"sessionId\":\"%s\","
        "\"enableRequest\":%s,\"targetRpm\":%d,\"requestedRpm\":%d,"
        "\"steeringTargetMdeg\":%ld,\"replayCached\":%s}",
        g_control.session_active ? "true" : "false", g_control.session_id,
        g_control.enable_request ? "true" : "false", (int)g_control.target_rpm,
        (int)g_control.requested_rpm, (long)g_control.target_steering_mdeg,
        (g_last_request_id[0] != '\0' || g_last_response[0] != '\0') ? "true" : "false");
    return written > 0 && written < size;
}
#if PATROL_CHASSIS_MQTT_LAB_TEST
// 在专用实验构建中植入可检查的旧网络状态。用于验证网络切换是否清除会话、目标和重放缓存；仅受实验编译开关控制，不作为正常控制入口。
void RemoteService_LabSeedNetworkState(void)
{
    taskENTER_CRITICAL();
    g_control.session_active = 1U;
    g_control.enable_request = 1U;
    g_control.target_rpm = 37;
    g_control.requested_rpm = 41;
    g_control.target_steering_mdeg = 3200;
    strcpy(g_control.session_id, "mqtt-lab-old-session");
    strcpy(g_last_request_id, "mqtt-lab-replay");
    strcpy(g_last_response, "cached");
    g_lab_hold_state = 1U;
    taskEXIT_CRITICAL();
    APP_LOGW("REMOTE", "lab-only non-actuating network state seeded");
}
#endif
// 将执行失败交给先撤使能后锁存的统一流程；pending 阶段已拒绝新运动，既有首因不会被派生错误覆盖。
void RemoteService_ExecutionFault(void)
{
    taskENTER_CRITICAL();
    ChassisControl_RequestFault(&g_control, CHASSIS_STOP_EXECUTION_FAULT);
    taskEXIT_CRITICAL();
}
// 由运行层在双节点撤使能完成或等待超期时正式锁存；本函数不发运动命令。
void RemoteService_CompleteFaultStop(void)
{
    taskENTER_CRITICAL();
    ChassisControl_CompleteFaultStop(&g_control);
    taskEXIT_CRITICAL();
}
// 初始化共享远控仲裁及请求缓存。根据 Config V5 填入标定有效性，保留内部行走禁用位并清空会话和重放记录；不会自动使能。
void RemoteService_Init(void)
{
    const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
    ChassisFeedback feedback = {0};
    uint8_t drive_brake = g_control.drive_brake;
    ChassisControl_Init(&g_control);
    ChassisControl_SetFaultStopHandler(&g_control, PatrolRuntime_RequestFaultStop);
    /* Fault reset/reinitialization cannot release a physical brake switch. */
    ChassisControl_SetDriveBrake(&g_control, drive_brake);
    feedback.calibrated = cfg != 0 && cfg->calibrated && PatrolConfig_CalibrationCompatible();
    feedback.feedback_ms = HAL_GetTick();
    ChassisControl_SetFeedback(&g_control, &feedback);
    g_last_request_id[0] = '\0';
    g_last_response[0] = '\0';
    memset(g_power_replay, 0, sizeof(g_power_replay));
    g_remote_initialized = 1U;
    APP_LOGI("REMOTE", "initialized calibrated=%u; outputs gated",
             (unsigned int)feedback.calibrated);
}

// 在远控任务上下文中推进安全检查。未初始化时返回，实验保持状态受编译开关限制；正常路径持临界区调用安全周期处理。
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

// 按指定单调时间推进控制层安全与租约状态。通过可嵌套临界区串行化控制状态，调用统一底盘 Tick 处理门禁、租约和换向确认。
void RemoteService_SafetyTick(uint32_t now_ms)
{
    if (!g_remote_initialized) return;
    taskENTER_CRITICAL();
    ChassisControl_Tick(&g_control, now_ms);
    taskEXIT_CRITICAL();
}

// 向已初始化的控制层发布最新反馈快照。空指针被忽略；上层须按运行层并发约定组织一致快照与访问保护。
void RemoteService_UpdateFeedback(const ChassisFeedback* feedback)
{
    if (g_remote_initialized && feedback != 0) ChassisControl_SetFeedback(&g_control, feedback);
}

// 设置受临界区保护的内部行走禁用位。只限制行走并取消待换向目标，不撤销转向会话；当前 T12 B 键不调用此接口。
void RemoteService_SetDriveBrake(uint8_t active)
{
    taskENTER_CRITICAL();
    ChassisControl_SetDriveBrake(&g_control, active);
    taskEXIT_CRITICAL();
}

// 处理实体遥控和 CAN2 等本地控制请求。与云端共用会话归属、租约和安全门禁；stop 保持可执行，其他动作不能在标定占用或安全条件不满足时进入。
uint8_t RemoteService_ApplyLocal(const char* action, const char* session_id,
    uint32_t seq, uint32_t lease_ms, int16_t rpm, int32_t steering_mdeg, uint32_t now_ms)
{
    uint8_t accepted = 0U;
    if (!g_remote_initialized || action == NULL || session_id == NULL || !session_id[0]) return 0U;
    taskENTER_CRITICAL();
    if (!strcmp(action, "stop")) {
        accepted = ChassisControl_Apply(&g_control, action, session_id, seq, lease_ms,
                                        rpm, steering_mdeg, now_ms);
    } else if (!CenterCalibration_IsActive() &&
               (!g_control.session_active || !strcmp(g_control.session_id, session_id)) &&
               ChassisControl_SafetyReady(&g_control, now_ms)) {
        accepted = ChassisControl_Apply(&g_control, action, session_id, seq, lease_ms,
                                        rpm, steering_mdeg, now_ms);
    }
    taskEXIT_CRITICAL();
    return accepted;
}

// 解析并执行公共 MQTT 底盘控制请求。核验身份、规范化摘要、重放、可信时钟和有效期后仲裁；有效 stop 和下电优先撤目标，响应只表明处理结果而非物理执行完成。
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
    uint8_t request_conflict = 0U;
    uint8_t stop_applied = 0U, power_cached = 0U;
    uint8_t digest[32];
    int power_slot = -1;
    ABoxClockSnapshot clock = TimeBase_GetSnapshot();
    if (!g_remote_initialized || payload == 0 || response == 0 || size == 0U) return 0U;
    parse_status = ChassisCloud_Parse(payload, length, &command);
    if (command.request_id[0] == '\0') return 0U;
    if (parse_status == CHASSIS_CLOUD_OK && !ChassisCloud_RequestDigest(&command, digest))
        parse_status = CHASSIS_CLOUD_INVALID;
    /* A valid safety stop takes effect even on replay or requestId conflict. */
    if (parse_status == CHASSIS_CLOUD_OK && command.type == CHASSIS_CLOUD_COMMAND_MOTION &&
        strcmp(command.action, "stop") == 0) {
        taskENTER_CRITICAL();
        stop_applied = ChassisControl_Apply(&g_control, command.action, command.session_id,
            command.seq, command.lease_ms, command.drive_rpm,
            command.steering_mdeg, HAL_GetTick());
        taskEXIT_CRITICAL();
    }
    /* Safety stop remains effective on replay and requestId conflict. */
    if (parse_status == CHASSIS_CLOUD_OK && command.type == CHASSIS_CLOUD_COMMAND_POWER_OFF) {
        RemoteService_Revoke();
        (void)VehiclePowerOutput_Set(0U);
    }
    if (parse_status == CHASSIS_CLOUD_OK) {
        for (unsigned i=0; i<4; ++i) {
            ABoxPowerReplay *e = &g_power_replay[i];
            if (e->request_id[0] && !strcmp(e->request_id,command.request_id)) {
                uint8_t original_digest[32];
                if (command.type != CHASSIS_CLOUD_COMMAND_POWER_ON ||
                    !ABoxMqttDigest_NullRequest("power_on", 1, e->expires_at, original_digest) ||
                    memcmp(digest, original_digest, sizeof(digest))) {
                    code = 4006U; message = "requestId conflict"; request_conflict = 1U;
                } else { code = e->code; message = code == 200U ? "operation accepted" : "power output unavailable";
                         power_cached = 1U; power_slot = (int)i; }
                goto format_response;
            }
            if (!e->request_id[0] || (clock.quality == ABOX_CLOCK_SYNCHRONIZED &&
                clock.mono_ms >= e->protect_mono && clock.utc_ms >= clock.error_ms &&
                clock.utc_ms - clock.error_ms >= e->protect_until)) power_slot = (int)i;
        }
    }
    if (strcmp(command.request_id, g_last_request_id) == 0 && g_last_response[0] != '\0')
    {
        if (parse_status == CHASSIS_CLOUD_OK && command.type == CHASSIS_CLOUD_COMMAND_MOTION &&
            !memcmp(digest, g_last_digest, sizeof(digest))) {
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
            if (command.has_expires_at && (code = ABoxMqttV4_CheckExpiry(&clock, command.expires_at)) != 200U) {
                message = code == 4003U ? "trusted time unavailable" : "request expired";
                goto format_response;
            }
            if (target_on && power_slot < 0) {
                code = 4003U; message = "idempotency cache full"; goto format_response;
            }
            if (!target_on) RemoteService_Revoke();
            if (!VehiclePowerOutput_IsAvailable()) {
                code = 5001U;
                message = "power output unavailable";
            } else if (!VehiclePowerOutput_Set(target_on)) {
                code = 5001U;
                message = "power output write failed";
            } else {
                if (target_on) {
                    taskENTER_CRITICAL(); g_control.auto_center_inhibited = 0U; taskEXIT_CRITICAL();
                }
                accepted = 1U;
                code = 200U;
                message = "operation accepted";
            }
            if (target_on) {
                ABoxPowerReplay *e = &g_power_replay[power_slot];
                strcpy(e->request_id,command.request_id); e->expires_at=command.expires_at;
                e->protect_until=ABoxMqttV4_IdempotencyProtectUntil(command.expires_at);
                uint64_t lower=clock.utc_ms > clock.error_ms ? clock.utc_ms-clock.error_ms : 0U;
                uint64_t remaining=e->protect_until > lower ? e->protect_until-lower : 0U;
                e->protect_mono=UINT64_MAX-clock.mono_ms < remaining ? UINT64_MAX : clock.mono_ms+remaining;
                e->code=code; e->power_on=VehiclePowerOutput_IsOn();
            }
            goto format_response;
        }
        if (strcmp(command.action, "renew") == 0)
            session_conflict = (uint8_t)(!g_control.session_active ||
                strcmp(g_control.session_id, command.session_id) != 0);
        else if (strcmp(command.action, "stop") != 0 && g_control.session_active &&
                 strcmp(g_control.session_id, command.session_id) != 0)
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
            accepted = stop_applied ? 1U : ChassisControl_Apply(&g_control, command.action, command.session_id,
                                            command.seq, command.lease_ms, command.drive_rpm,
                                            command.steering_mdeg, HAL_GetTick());
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
            (power_cached ? g_power_replay[power_slot].power_on : VehiclePowerOutput_IsOn()) ? "true" : "false");
    else
        written = snprintf(response, size,
            "{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{"
            "\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\",\"result\":{\"targetDriveRpm\":%d,"
            "\"targetSteeringDeg\":%s,\"stopReason\":%u}}}",
            timestamp, command.request_id, (unsigned int)code, message,
            (int)g_control.target_rpm, steering_value, (unsigned int)g_control.stop_reason);
    if (written <= 0 || written >= size) return 0U;
    if (!request_conflict && parse_status == CHASSIS_CLOUD_OK && command.type == CHASSIS_CLOUD_COMMAND_MOTION) {
        strncpy(g_last_request_id, command.request_id, sizeof(g_last_request_id) - 1U);
        g_last_request_id[sizeof(g_last_request_id) - 1U] = '\0';
        memcpy(g_last_digest, digest, sizeof(digest));
        if ((size_t)written < sizeof(g_last_response)) strcpy(g_last_response, response);
    }
    return 1U;
}

// 返回共享底盘控制上下文的只读视图。指针指向模块静态存储，会被其他任务更新；跨字段一致性读取需遵守临界区约定，不得保存为独立历史快照。
const ChassisControl* RemoteService_State(void) { return &g_control; }
