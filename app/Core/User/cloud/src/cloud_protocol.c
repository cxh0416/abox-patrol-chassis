#include "cloud_protocol.h"
#include "vehicle_steering_profile.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static uint8_t request_id_valid(const char* value)
{
    uint16_t i;
    if ((value == 0) || (value[0] == '\0')) return 0U;
    for (i = 0U; i < CLOUD_PROTOCOL_REQUEST_ID_SIZE; i++)
    {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return 1U;
        if ((ch < 0x21U) || (ch > 0x7EU)) return 0U;
    }
    return 0U;
}

static uint8_t version_valid(const char* value)
{
    static const char prefix[] = "patrol-chassis-";
    uint16_t i;
    if (value == 0 || strncmp(value, prefix, sizeof(prefix) - 1U) != 0) return 0U;
    for (i = (uint16_t)(sizeof(prefix) - 1U); i < CLOUD_PROTOCOL_OTA_VERSION_SIZE; i++) {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return i > sizeof(prefix) + 1U;
        if (!((ch >= '0' && ch <= '9') || ch == '.')) return 0U;
    }
    return 0U;
}

static uint8_t config_string_valid(const char* value, uint16_t capacity, uint8_t vid)
{
    uint16_t i;
    if (value == 0 || value[0] == '\0') return 0U;
    for (i = 0U; i < capacity; i++) {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return 1U;
        if (ch < 0x21U || ch > 0x7EU || ch == '"' || ch == '\\') return 0U;
        if (vid && !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                     ch == '.' || ch == '_' || ch == '-')) return 0U;
    }
    return 0U;
}

static uint8_t json_integer_between(const cJSON* item, double minimum, double maximum)
{
    double value;
    int64_t integer;
    if (!cJSON_IsNumber(item)) return 0U;
    value = item->valuedouble;
    if (value < minimum || value > maximum) return 0U;
    integer = (int64_t)value;
    return value == (double)integer ? 1U : 0U;
}

static void extract_request_id(const char* payload, uint16_t length, char* output)
{
    static const char key[] = "\"requestId\"";
    uint16_t i;
    output[0] = '\0';
    if ((payload == 0) || (length < sizeof(key))) return;
    for (i = 0U; (uint32_t)i + sizeof(key) - 1U < length; i++)
    {
        uint16_t pos;
        uint16_t used = 0U;
        if (memcmp(payload + i, key, sizeof(key) - 1U) != 0) continue;
        pos = (uint16_t)(i + sizeof(key) - 1U);
        while ((pos < length) && ((payload[pos] == ' ') || (payload[pos] == '\t') || (payload[pos] == '\r') ||
                                  (payload[pos] == '\n') || (payload[pos] == ':'))) pos++;
        if ((pos >= length) || (payload[pos++] != '"')) return;
        while ((pos < length) && (payload[pos] != '"') && (used < CLOUD_PROTOCOL_REQUEST_ID_SIZE - 1U))
        {
            if (payload[pos] == '\\') return;
            output[used++] = payload[pos++];
        }
        output[used] = '\0';
        if (!request_id_valid(output)) output[0] = '\0';
        return;
    }
}

CloudProtocolStatus_t CloudProtocol_Parse(const char* payload, uint16_t length, uint8_t ota_topic,
                                          CloudProtocolRequest_t* request)
{
    cJSON* root;
    cJSON* version;
    cJSON* data;
    cJSON* product;
    cJSON* request_id;
    cJSON* command;
    cJSON* params;
    cJSON* action;
    cJSON* url;
    cJSON* ota_version;
    cJSON* size;
    cJSON* crc32;
    cJSON* mqtt_vid;
    cJSON* mqtt_host;
    cJSON* mqtt_port;
    cJSON* mqtt_username;
    cJSON* mqtt_password;
    cJSON* mqtt_tls_enabled;
    cJSON* mqtt_tls_profile_id;
    cJSON* steering_center;
    cJSON* absolute_center;
    cJSON* steering_counts_per_degree;
    cJSON* absolute_counts_per_degree;
    cJSON* steering_direction;
    cJSON* absolute_direction;
    cJSON* steering_tolerance;
    cJSON* center_session;
    cJSON* center_delta;
    CloudProtocolStatus_t status = CLOUD_PROTOCOL_INVALID;

    if (request == 0) return CLOUD_PROTOCOL_INVALID;
    memset(request, 0, sizeof(*request));
    if (payload == 0) return CLOUD_PROTOCOL_INVALID;
    extract_request_id(payload, length, request->request_id);
    if (length > CLOUD_PROTOCOL_MAX_PAYLOAD) return CLOUD_PROTOCOL_TOO_LARGE;

    root = cJSON_ParseWithLength(payload, length);
    if (root == 0) return CLOUD_PROTOCOL_INVALID;
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    product = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "product") : 0;
    request_id = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "requestId") : 0;
    command = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "command") : 0;
    params = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "params") : 0;
    if (!cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0 || !cJSON_IsObject(data) ||
        !cJSON_IsString(request_id) || !request_id_valid(request_id->valuestring) || !cJSON_IsString(command) ||
        !cJSON_IsObject(params)) goto done;
    if (product != 0 && (!cJSON_IsString(product) ||
        strcmp(product->valuestring, "patrol_vehicle_chassis") != 0)) goto done;
    strncpy(request->request_id, request_id->valuestring, sizeof(request->request_id) - 1U);
    request->request_id[sizeof(request->request_id) - 1U] = '\0';

    if (ota_topic)
    {
        if (strcmp(command->valuestring, "ota_upgrade") != 0) goto done;
        url = cJSON_GetObjectItemCaseSensitive(params, "url");
        ota_version = cJSON_GetObjectItemCaseSensitive(params, "version");
        size = cJSON_GetObjectItemCaseSensitive(params, "size");
        crc32 = cJSON_GetObjectItemCaseSensitive(params, "crc32");
        if (!cJSON_IsString(url) || !cJSON_IsString(ota_version) || !cJSON_IsNumber(size) || !cJSON_IsString(crc32) ||
            strlen(url->valuestring) >= sizeof(request->ota_url) || !version_valid(ota_version->valuestring) ||
            size->valuedouble < 1 || size->valuedouble > 0x36000 || strlen(crc32->valuestring) != 8U) goto done;
        for (uint8_t i = 0U; i < 8U; i++) {
            char ch = crc32->valuestring[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) goto done;
        }
        strncpy(request->ota_url, url->valuestring, sizeof(request->ota_url) - 1U);
        strncpy(request->ota_version, ota_version->valuestring, sizeof(request->ota_version) - 1U);
        request->ota_size = (uint32_t)size->valuedouble;
        {
            unsigned long parsed_crc = 0UL;
            if (sscanf(crc32->valuestring, "%lx", &parsed_crc) != 1) goto done;
            request->ota_crc32 = (uint32_t)parsed_crc;
        }
        request->action = CLOUD_PROTOCOL_ACTION_OTA;
        status = CLOUD_PROTOCOL_OK;
        goto done;
    }
    if (strcmp(command->valuestring, "internal_config") != 0) goto done;
    action = cJSON_GetObjectItemCaseSensitive(params, "action");
    if (!cJSON_IsString(action)) goto done;
    if (strcmp(action->valuestring, "get_info") == 0) request->action = CLOUD_PROTOCOL_ACTION_GET_INFO;
    else if (strcmp(action->valuestring, "get_can_diag") == 0) request->action = CLOUD_PROTOCOL_ACTION_GET_CAN_DIAG;
    else if (strcmp(action->valuestring, "get_calibration") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_GET_CALIBRATION;
    else if (strcmp(action->valuestring, "clear_calibration") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_CLEAR_CALIBRATION;
    else if (strcmp(action->valuestring, "set_calibration") == 0)
    {
        steering_center = cJSON_GetObjectItemCaseSensitive(params, "steeringCenterCount");
        absolute_center = cJSON_GetObjectItemCaseSensitive(params, "absoluteCenterCount");
        steering_counts_per_degree = cJSON_GetObjectItemCaseSensitive(params, "steeringCountsPerDegreeMilli");
        absolute_counts_per_degree = cJSON_GetObjectItemCaseSensitive(params, "absoluteCountsPerDegreeMilli");
        steering_direction = cJSON_GetObjectItemCaseSensitive(params, "steeringDirection");
        absolute_direction = cJSON_GetObjectItemCaseSensitive(params, "absoluteDirection");
        steering_tolerance = cJSON_GetObjectItemCaseSensitive(params, "steeringToleranceMdeg");
        if (!json_integer_between(steering_center, -2147483648.0, 2147483647.0) ||
            !json_integer_between(absolute_center, 0.0, 4294967295.0)) goto done;
        /* Missing profile fields select the product profile. Supplied fields must match it. */
        if ((steering_counts_per_degree && !json_integer_between(steering_counts_per_degree, VEHICLE_STEERING_COUNTS_MILLI, VEHICLE_STEERING_COUNTS_MILLI)) ||
            (absolute_counts_per_degree && !json_integer_between(absolute_counts_per_degree, VEHICLE_ABSOLUTE_COUNTS_MILLI, VEHICLE_ABSOLUTE_COUNTS_MILLI)) ||
            (steering_direction && !json_integer_between(steering_direction, 1, 1)) ||
            (absolute_direction && !json_integer_between(absolute_direction, 1, 1)) ||
            (steering_tolerance && !json_integer_between(steering_tolerance, VEHICLE_STEERING_TOLERANCE_MDEG, VEHICLE_STEERING_TOLERANCE_MDEG))) goto done;
        request->steering_center_count = (int32_t)steering_center->valuedouble;
        request->absolute_center_count = (uint32_t)absolute_center->valuedouble;
        request->steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
        request->absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
        request->steering_direction = VEHICLE_STEERING_DIRECTION;
        request->absolute_direction = VEHICLE_ABSOLUTE_DIRECTION;
        request->steering_tolerance_mdeg = VEHICLE_STEERING_TOLERANCE_MDEG;
        request->action = CLOUD_PROTOCOL_ACTION_SET_CALIBRATION;
    }
    else if (strcmp(action->valuestring, "reset_motion_fault") == 0) request->action = CLOUD_PROTOCOL_ACTION_RESET_MOTION_FAULT;
    else if (strcmp(action->valuestring, "get_bench_steering") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_BENCH_GET;
    else if (strcmp(action->valuestring, "start_center_calibration") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_CENTER_CAL_START;
    else if (strcmp(action->valuestring, "get_center_calibration") == 0)
    {
        center_session = cJSON_GetObjectItemCaseSensitive(params, "sessionId");
        if (!cJSON_IsString(center_session) || center_session->valuestring[0] == '\0' ||
            strlen(center_session->valuestring) >= sizeof(request->center_session_id)) goto done;
        strcpy(request->center_session_id, center_session->valuestring);
        request->action = CLOUD_PROTOCOL_ACTION_CENTER_CAL_GET;
    }
    else if (strcmp(action->valuestring, "jog_center_calibration") == 0)
    {
        center_session = cJSON_GetObjectItemCaseSensitive(params, "sessionId");
        center_delta = cJSON_GetObjectItemCaseSensitive(params, "deltaMdeg");
        if (!cJSON_IsString(center_session) || center_session->valuestring[0] == '\0' ||
            strlen(center_session->valuestring) >= sizeof(request->center_session_id) ||
            !json_integer_between(center_delta, -5000, 5000) ||
            (center_delta->valueint != -1000 && center_delta->valueint != -200 &&
             center_delta->valueint != 200 && center_delta->valueint != 1000 &&
             center_delta->valueint != -5000 && center_delta->valueint != 5000)) goto done;
        strcpy(request->center_session_id, center_session->valuestring);
        request->center_delta_mdeg = (int16_t)center_delta->valueint;
        request->action = CLOUD_PROTOCOL_ACTION_CENTER_CAL_JOG;
    }
    else if (strcmp(action->valuestring, "commit_center_calibration") == 0 ||
             strcmp(action->valuestring, "cancel_center_calibration") == 0)
    {
        center_session = cJSON_GetObjectItemCaseSensitive(params, "sessionId");
        if (!cJSON_IsString(center_session) || center_session->valuestring[0] == '\0' ||
            strlen(center_session->valuestring) >= sizeof(request->center_session_id)) goto done;
        strcpy(request->center_session_id, center_session->valuestring);
        request->action = strcmp(action->valuestring, "commit_center_calibration") == 0 ?
            CLOUD_PROTOCOL_ACTION_CENTER_CAL_COMMIT : CLOUD_PROTOCOL_ACTION_CENTER_CAL_CANCEL;
    }
    else if (strcmp(action->valuestring, "stop_bench_steering") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_BENCH_STOP;
    else if (strcmp(action->valuestring, "arm_bench_steering") == 0 ||
             strcmp(action->valuestring, "arm_bench_drive") == 0)
    {
        cJSON* limit = cJSON_GetObjectItemCaseSensitive(params, "limitMdeg");
        if (!json_integer_between(limit, 1000, VEHICLE_MAX_STEERING_MDEG) ||
            (limit->valueint != 1000 && limit->valueint != 3000 && limit->valueint != 5000 &&
             limit->valueint != 20000 && limit->valueint != 30000)) goto done;
        request->bench_limit_mdeg = (uint16_t)limit->valueint;
        request->bench_drive = strcmp(action->valuestring, "arm_bench_drive") == 0;
        request->action = CLOUD_PROTOCOL_ACTION_BENCH_ARM;
    }
    else if (strcmp(action->valuestring, "get_log") == 0) request->action = CLOUD_PROTOCOL_ACTION_GET_LOG;
    else if (strcmp(action->valuestring, "reboot") == 0) request->action = CLOUD_PROTOCOL_ACTION_REBOOT;
    else if (strcmp(action->valuestring, "power_on") == 0) request->action = CLOUD_PROTOCOL_ACTION_POWER_ON;
    else if (strcmp(action->valuestring, "power_off") == 0) request->action = CLOUD_PROTOCOL_ACTION_POWER_OFF;
    else if (strcmp(action->valuestring, "mqtt_runtime_get") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_GET;
    else if (strcmp(action->valuestring, "mqtt_lab_seed_control") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_LAB_SEED_CONTROL;
    else if (strcmp(action->valuestring, "mqtt_runtime_candidate") == 0)
    {
        mqtt_host = cJSON_GetObjectItemCaseSensitive(params, "host");
        mqtt_port = cJSON_GetObjectItemCaseSensitive(params, "port");
        mqtt_username = cJSON_GetObjectItemCaseSensitive(params, "username");
        mqtt_password = cJSON_GetObjectItemCaseSensitive(params, "password");
        mqtt_tls_enabled = cJSON_GetObjectItemCaseSensitive(params, "tlsEnabled");
        mqtt_tls_profile_id = cJSON_GetObjectItemCaseSensitive(params, "tlsProfileId");
        if (!cJSON_IsString(mqtt_host) || !cJSON_IsNumber(mqtt_port) ||
            !cJSON_IsString(mqtt_username) || !cJSON_IsString(mqtt_password) ||
            !cJSON_IsBool(mqtt_tls_enabled) || !json_integer_between(mqtt_tls_profile_id, 0, 255) ||
            !config_string_valid(mqtt_host->valuestring, sizeof(request->mqtt_host), 0U) ||
            !config_string_valid(mqtt_username->valuestring, sizeof(request->mqtt_username), 0U) ||
            !config_string_valid(mqtt_password->valuestring, sizeof(request->mqtt_password), 0U) ||
            !json_integer_between(mqtt_port, 1, 65535)) goto done;
        strncpy(request->mqtt_host, mqtt_host->valuestring, sizeof(request->mqtt_host) - 1U);
        strncpy(request->mqtt_username, mqtt_username->valuestring, sizeof(request->mqtt_username) - 1U);
        strncpy(request->mqtt_password, mqtt_password->valuestring, sizeof(request->mqtt_password) - 1U);
        request->mqtt_port = (uint16_t)mqtt_port->valuedouble;
        request->mqtt_tls_enabled = cJSON_IsTrue(mqtt_tls_enabled) ? 1U : 0U;
        request->mqtt_tls_profile_id = (uint8_t)mqtt_tls_profile_id->valueint;
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_CANDIDATE;
    }
#if PATROL_CHASSIS_MQTT_LAB_TEST
    else if (strcmp(action->valuestring, "mqtt_runtime_cancel") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_CANCEL;
#endif
    else if (strcmp(action->valuestring, "set_mqtt") == 0)
    {
        mqtt_vid = cJSON_GetObjectItemCaseSensitive(params, "vid");
        mqtt_host = cJSON_GetObjectItemCaseSensitive(params, "host");
        mqtt_port = cJSON_GetObjectItemCaseSensitive(params, "port");
        mqtt_username = cJSON_GetObjectItemCaseSensitive(params, "username");
        mqtt_password = cJSON_GetObjectItemCaseSensitive(params, "password");
        mqtt_tls_enabled = cJSON_GetObjectItemCaseSensitive(params, "tlsEnabled");
        mqtt_tls_profile_id = cJSON_GetObjectItemCaseSensitive(params, "tlsProfileId");
        if (!cJSON_IsString(mqtt_vid) || !cJSON_IsString(mqtt_host) || !cJSON_IsNumber(mqtt_port) ||
            !cJSON_IsString(mqtt_username) || !cJSON_IsString(mqtt_password) ||
            !cJSON_IsBool(mqtt_tls_enabled) || !json_integer_between(mqtt_tls_profile_id, 0, 255) ||
            !config_string_valid(mqtt_vid->valuestring, sizeof(request->mqtt_vid), 1U) ||
            !config_string_valid(mqtt_host->valuestring, sizeof(request->mqtt_host), 0U) ||
            !config_string_valid(mqtt_username->valuestring, sizeof(request->mqtt_username), 0U) ||
            !config_string_valid(mqtt_password->valuestring, sizeof(request->mqtt_password), 0U) ||
            !json_integer_between(mqtt_port, 1, 65535)) goto done;
        strncpy(request->mqtt_vid, mqtt_vid->valuestring, sizeof(request->mqtt_vid) - 1U);
        strncpy(request->mqtt_host, mqtt_host->valuestring, sizeof(request->mqtt_host) - 1U);
        strncpy(request->mqtt_username, mqtt_username->valuestring, sizeof(request->mqtt_username) - 1U);
        strncpy(request->mqtt_password, mqtt_password->valuestring, sizeof(request->mqtt_password) - 1U);
        request->mqtt_port = (uint16_t)mqtt_port->valuedouble;
        request->mqtt_tls_enabled = cJSON_IsTrue(mqtt_tls_enabled) ? 1U : 0U;
        request->mqtt_tls_profile_id = (uint8_t)mqtt_tls_profile_id->valueint;
        request->action = CLOUD_PROTOCOL_ACTION_SET_MQTT;
    }
    else { status = CLOUD_PROTOCOL_UNKNOWN_ACTION; goto done; }
    status = CLOUD_PROTOCOL_OK;

done:
    cJSON_Delete(root);
    return status;
}

const char* CloudProtocol_StatusMessage(CloudProtocolStatus_t status)
{
    switch (status)
    {
        case CLOUD_PROTOCOL_UNKNOWN_ACTION: return "UNKNOWN_ACTION";
        case CLOUD_PROTOCOL_TOO_LARGE: return "PAYLOAD_TOO_LARGE";
        case CLOUD_PROTOCOL_INVALID: return "INVALID_REQUEST";
        case CLOUD_PROTOCOL_OK:
        default: return "OK";
    }
}
