#include "chassis_cloud_protocol.h"

#include <string.h>

#include "cJSON.h"

static uint8_t copy_identifier(char* output, uint16_t capacity, const cJSON* item)
{
    size_t length;
    if (!cJSON_IsString(item) || item->valuestring == 0) return 0U;
    length = strlen(item->valuestring);
    if (length == 0U || length >= capacity) return 0U;
    memcpy(output, item->valuestring, length + 1U);
    return 1U;
}

uint8_t ChassisCloud_Parse(const char* payload, uint16_t length, ChassisCloudCommand* command)
{
    cJSON *root, *version, *data, *request_id, *device_type, *name, *params;
    cJSON *action, *session_id, *seq, *lease, *rpm, *angle;
    uint8_t valid = 0U;
    if (payload == 0 || command == 0 || length == 0U || length > 1023U) return 0U;
    memset(command, 0, sizeof(*command));
    root = cJSON_ParseWithLength(payload, length);
    if (root == 0) return 0U;
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    request_id = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "requestId") : 0;
    device_type = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "deviceType") : 0;
    name = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "command") : 0;
    params = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "params") : 0;
    if (!cJSON_IsString(version) || strcmp(version->valuestring, "3.0") != 0 ||
        !cJSON_IsString(device_type) || strcmp(device_type->valuestring, "patrol_vehicle_chassis") != 0 ||
        !cJSON_IsString(name) || strcmp(name->valuestring, "chassis_control") != 0 || !cJSON_IsObject(params) ||
        !copy_identifier(command->request_id, sizeof(command->request_id), request_id)) goto done;
    action = cJSON_GetObjectItemCaseSensitive(params, "action");
    session_id = cJSON_GetObjectItemCaseSensitive(params, "sessionId");
    seq = cJSON_GetObjectItemCaseSensitive(params, "seq");
    lease = cJSON_GetObjectItemCaseSensitive(params, "leaseMs");
    rpm = cJSON_GetObjectItemCaseSensitive(params, "driveRpm");
    angle = cJSON_GetObjectItemCaseSensitive(params, "steeringDeg");
    if (!copy_identifier(command->action, sizeof(command->action), action) ||
        !copy_identifier(command->session_id, sizeof(command->session_id), session_id) ||
        (!cJSON_IsNumber(seq)) || seq->valuedouble < 1 || seq->valuedouble > 4294967295.0 ||
        (!cJSON_IsNumber(lease)) || lease->valuedouble < 1 || lease->valuedouble > 1000 ||
        (!cJSON_IsNumber(rpm)) || rpm->valuedouble < -300 || rpm->valuedouble > 300 ||
        (!cJSON_IsNumber(angle)) || angle->valuedouble < -5 || angle->valuedouble > 5) goto done;
    if (strcmp(command->action, "acquire") != 0 && strcmp(command->action, "renew") != 0 &&
        strcmp(command->action, "stop") != 0) goto done;
    command->seq = (uint32_t)seq->valuedouble;
    command->lease_ms = (uint32_t)lease->valuedouble;
    command->drive_rpm = (int16_t)rpm->valuedouble;
    command->steering_mdeg = (int32_t)(angle->valuedouble * 1000.0);
    valid = 1U;
done:
    cJSON_Delete(root);
    return valid;
}

