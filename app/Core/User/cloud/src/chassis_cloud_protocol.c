#include "chassis_cloud_protocol.h"

#include <string.h>

#include "cJSON.h"
#include "abox_mqtt_v4.h"
#include "chassis_control.h"
#include "abox_mqtt_digest.h"

/* steeringDeg is the only product Number field; other numeric fields are Integer. */
static int number_field(void *context, const cJSON *root, const cJSON *number)
{
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *params = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "params") : 0;
    (void)context;
    return cJSON_IsObject(params) && number == cJSON_GetObjectItemCaseSensitive(params, "steeringDeg");
}

uint16_t ChassisCloud_DecodeRequest(ABoxMqttRequest *request, const char *payload, uint16_t length)
{
    return ABoxMqttRequest_Decode(request, payload, length, number_field, 0);
}
// 向规范化摘要写入器提供运动参数。按规定类型和顺序输出参数，使重放检查与业务解析使用相同语义，而非原始 JSON 排版。
static int motion_params(void *value, ABoxCanonicalWriter write, void *context)
{
    const ChassisCloudCommand *command = value;
    return write(context, "{\"action\":", 10) &&
        ABoxMqttDigest_WriteString(command->action, write, context) &&
        write(context, ",\"driveRpm\":", 12) && ABoxMqttDigest_WriteInteger(command->drive_rpm, write, context) &&
        write(context, ",\"leaseMs\":", 11) && ABoxMqttDigest_WriteInteger(command->lease_ms, write, context) &&
        write(context, ",\"seq\":", 7) && ABoxMqttDigest_WriteInteger(command->seq, write, context) &&
        write(context, ",\"sessionId\":", 13) && ABoxMqttDigest_WriteString(command->session_id, write, context) &&
        write(context, ",\"steeringDeg\":", 15) && ABoxMqttDigest_WriteMilli(command->steering_mdeg, write, context) &&
        write(context, "}", 1);
}
// 生成底盘命令的规范化 32 字节摘要。供同 requestId 的重放和冲突检测使用，摘要只基于正式命令字段，不代表授权或签名验证。
uint8_t ChassisCloud_RequestDigest(const ChassisCloudCommand *command, uint8_t digest[32])
{
    if (!command) return 0;
    if (command->type == CHASSIS_CLOUD_COMMAND_MOTION)
        return (uint8_t)ABoxMqttDigest_RequestStream("chassis_control", 0, 0, motion_params, (void *)command, digest);
    if (command->type != CHASSIS_CLOUD_COMMAND_POWER_ON && command->type != CHASSIS_CLOUD_COMMAND_POWER_OFF) return 0;
    return (uint8_t)ABoxMqttDigest_NullRequest(command->type == CHASSIS_CLOUD_COMMAND_POWER_ON ? "power_on" : "power_off",
        command->has_expires_at, command->expires_at, digest);
}
// 校验并复制协议标识字符串。限制字符和长度，保留终止符空间；失败不把被截断标识当成有效身份。
static uint8_t copy_identifier(char* output, uint16_t capacity, const cJSON* item)
{
    size_t length;
    if (!cJSON_IsString(item) || item->valuestring == 0) return 0U;
    length = strlen(item->valuestring);
    if (length == 0U || length >= capacity) return 0U;
    memcpy(output, item->valuestring, length + 1U);
    return 1U;
}

// 将公共请求 JSON 转为底盘类型化命令。检查长度、结构、重复键、身份和各动作参数，输出解析状态；本函数不授予控制权、不写硬件。
ChassisCloudStatus ChassisCloud_Parse(const char* payload, uint16_t length,
                                      ChassisCloudCommand* command)
{
    ABoxMqttRequest request;
    const cJSON *params, *action, *session_id, *seq, *lease, *rpm, *angle;
    double steering_mdeg;
    int32_t rounded_mdeg;
    ChassisCloudStatus status = CHASSIS_CLOUD_INVALID;
    if (command == 0) return status;
    memset(command, 0, sizeof(*command));
    if (ChassisCloud_DecodeRequest(&request, payload, length) != 200U) {
        if (request.request_id) {
            size_t id_length = strlen(request.request_id);
            if (id_length < sizeof(command->request_id)) memcpy(command->request_id, request.request_id, id_length + 1U);
        }
        ABoxMqttRequest_Close(&request);
        return status;
    }
    memcpy(command->request_id, request.request_id, strlen(request.request_id) + 1U);
    command->timestamp_ms = request.timestamp;
    params = request.params;
    if (strcmp(request.command, "power_on") == 0 || strcmp(request.command, "power_off") == 0) {
        if (!cJSON_IsNull(params)) goto done;
        command->type = strcmp(request.command, "power_on") == 0 ?
            CHASSIS_CLOUD_COMMAND_POWER_ON : CHASSIS_CLOUD_COMMAND_POWER_OFF;
        if (command->type == CHASSIS_CLOUD_COMMAND_POWER_ON && !request.has_expires_at) goto done;
        if (request.has_expires_at) {
            if (!ABoxMqttV4_RequestLifetimeValid(command->timestamp_ms,
                  request.expires_at,
                  command->type == CHASSIS_CLOUD_COMMAND_POWER_ON ? 5000U : 0U)) goto done;
            command->expires_at = request.expires_at;
            command->has_expires_at = 1U;
        }
        status = CHASSIS_CLOUD_OK;
        goto done;
    }
    if (strcmp(request.command, "chassis_control") != 0) {
        status = CHASSIS_CLOUD_UNSUPPORTED;
        goto done;
    }
    if (!cJSON_IsObject(params) || request.has_expires_at) goto done;
    action = cJSON_GetObjectItemCaseSensitive(params, "action");
    session_id = cJSON_GetObjectItemCaseSensitive(params, "sessionId");
    seq = cJSON_GetObjectItemCaseSensitive(params, "seq");
    lease = cJSON_GetObjectItemCaseSensitive(params, "leaseMs");
    rpm = cJSON_GetObjectItemCaseSensitive(params, "driveRpm");
    angle = cJSON_GetObjectItemCaseSensitive(params, "steeringDeg");
    if (!copy_identifier(command->action, sizeof(command->action), action) ||
        !copy_identifier(command->session_id, sizeof(command->session_id), session_id) ||
        (!cJSON_IsNumber(seq)) || seq->valuedouble < 1 || seq->valuedouble > 4294967295.0 ||
        seq->valuedouble != (double)(uint32_t)seq->valuedouble ||
        (!cJSON_IsNumber(lease)) || lease->valuedouble < 1 ||
        lease->valuedouble > CHASSIS_MAX_LEASE_MS ||
        lease->valuedouble != (double)(uint32_t)lease->valuedouble ||
        (!cJSON_IsNumber(rpm)) || rpm->valuedouble < -CHASSIS_MAX_RPM ||
        rpm->valuedouble > CHASSIS_MAX_RPM ||
        rpm->valuedouble != (double)(int16_t)rpm->valuedouble ||
        (!cJSON_IsNumber(angle)) ||
        angle->valuedouble < -((double)CHASSIS_MAX_STEERING_MDEG / 1000.0) ||
        angle->valuedouble > ((double)CHASSIS_MAX_STEERING_MDEG / 1000.0)) goto done;
    steering_mdeg = angle->valuedouble * 1000.0;
    rounded_mdeg = (int32_t)(steering_mdeg + (steering_mdeg < 0.0 ? -0.5 : 0.5));
    /* Compare the input binary64 with the exact millidegree value converted
     * back to binary64; multiplication alone can introduce rounding error. */
    if (angle->valuedouble != (double)rounded_mdeg / 1000.0) goto done;
    if (strcmp(command->action, "acquire") != 0 && strcmp(command->action, "renew") != 0 &&
        strcmp(command->action, "stop") != 0) {
        status = CHASSIS_CLOUD_UNSUPPORTED;
        goto done;
    }
    command->seq = (uint32_t)seq->valuedouble;
    command->lease_ms = (uint32_t)lease->valuedouble;
    command->drive_rpm = (int16_t)rpm->valuedouble;
    command->steering_mdeg = rounded_mdeg;
    command->type = CHASSIS_CLOUD_COMMAND_MOTION;
    status = CHASSIS_CLOUD_OK;
done:
    ABoxMqttRequest_Close(&request);
    return status;
}
