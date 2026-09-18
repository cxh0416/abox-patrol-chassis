#include "chassis_cloud_protocol.h"

#include <string.h>

#include "cJSON.h"
#include "chassis_control.h"

static uint8_t copy_identifier(char* output, uint16_t capacity, const cJSON* item)
{
    size_t length;
    if (!cJSON_IsString(item) || item->valuestring == 0) return 0U;
    length = strlen(item->valuestring);
    if (length == 0U || length >= capacity) return 0U;
    memcpy(output, item->valuestring, length + 1U);
    return 1U;
}

ChassisCloudStatus ChassisCloud_Parse(const char* payload, uint16_t length,
                                      ChassisCloudCommand* command)
{
    cJSON *root, *version, *data, *request_id, *name, *params;
    cJSON *action, *session_id, *seq, *lease, *rpm, *angle;
    double steering_mdeg;
    ChassisCloudStatus status = CHASSIS_CLOUD_INVALID;
    if (payload == 0 || command == 0 || length == 0U || length > 1023U) return status;
    memset(command, 0, sizeof(*command));
    root = cJSON_ParseWithLength(payload, length);
    if (root == 0) return status;
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    request_id = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "requestId") : 0;
    name = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "command") : 0;
    params = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "params") : 0;
    (void)copy_identifier(command->request_id, sizeof(command->request_id), request_id);
    if (!cJSON_IsString(version) || strcmp(version->valuestring, "4.0") != 0 ||
        command->request_id[0] == '\0') goto done;
    if (!cJSON_IsString(name)) {
        status = CHASSIS_CLOUD_UNSUPPORTED;
        goto done;
    }
    if (strcmp(name->valuestring, "power_on") == 0 || strcmp(name->valuestring, "power_off") == 0) {
        if (!cJSON_IsNull(params)) goto done;
        command->type = strcmp(name->valuestring, "power_on") == 0 ?
            CHASSIS_CLOUD_COMMAND_POWER_ON : CHASSIS_CLOUD_COMMAND_POWER_OFF;
        status = CHASSIS_CLOUD_OK;
        goto done;
    }
    if (strcmp(name->valuestring, "chassis_control") != 0) {
        status = CHASSIS_CLOUD_UNSUPPORTED;
        goto done;
    }
    if (!cJSON_IsObject(params)) goto done;
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
    if (steering_mdeg != (double)(int32_t)steering_mdeg) goto done;
    if (strcmp(command->action, "acquire") != 0 && strcmp(command->action, "renew") != 0 &&
        strcmp(command->action, "stop") != 0) {
        status = CHASSIS_CLOUD_UNSUPPORTED;
        goto done;
    }
    command->seq = (uint32_t)seq->valuedouble;
    command->lease_ms = (uint32_t)lease->valuedouble;
    command->drive_rpm = (int16_t)rpm->valuedouble;
    command->steering_mdeg = (int32_t)(angle->valuedouble * 1000.0);
    command->type = CHASSIS_CLOUD_COMMAND_MOTION;
    status = CHASSIS_CLOUD_OK;
done:
    cJSON_Delete(root);
    return status;
}
