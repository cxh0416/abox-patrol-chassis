#include "cloud_protocol.h"
#include "vehicle_steering_profile.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "abox_cloud_internal_v2.h"

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

#if PATROL_CHASSIS_MQTT_LAB_TEST
static uint8_t config_string_valid(const char* value, uint16_t capacity, uint8_t vid)
{
    uint16_t i;
    if (value == 0 || value[0] == '\0') return 0U;
    for (i = 0U; i < capacity; i++) {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return 1U;
        if (ch < 0x21U || ch > 0x7EU || ch == '"' || ch == '\\') return 0U;
        if (vid && !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                       ch == '_' || ch == '-')) return 0U;
    }
    return 0U;
}

#endif
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

CloudProtocolStatus_t CloudProtocol_Parse(const char* payload, uint16_t length, uint8_t ota_topic,
                                          CloudProtocolRequest_t* request)
{
    ABoxCloudInternalV2Envelope envelope;
    cJSON* command;
    cJSON* params;
    cJSON* action;
    cJSON* url;
    cJSON* ota_version;
    cJSON* size;
    cJSON* crc32;
#if PATROL_CHASSIS_MQTT_LAB_TEST
    cJSON* mqtt_host;
    cJSON* mqtt_port;
    cJSON* mqtt_username;
    cJSON* mqtt_password;
    cJSON* mqtt_tls_enabled;
    cJSON* mqtt_tls_profile_id;
#endif
    cJSON* center_session;
    cJSON* center_delta;
    CloudProtocolStatus_t status = CLOUD_PROTOCOL_INVALID;

    if (request == 0) return CLOUD_PROTOCOL_INVALID;
    memset(request, 0, sizeof(*request));
    if (payload == 0) return CLOUD_PROTOCOL_INVALID;
    ABoxCloudRequestId_ExtractRaw(payload, length, request->request_id,
                                   sizeof(request->request_id));
    if (length > CLOUD_PROTOCOL_MAX_PAYLOAD) return CLOUD_PROTOCOL_TOO_LARGE;

    if (!ABoxCloudInternalV2_Open(payload, length, "patrol_vehicle_chassis",
                                  sizeof(request->request_id), &envelope)) return CLOUD_PROTOCOL_INVALID;
    command = envelope.command;
    params = envelope.params;
    strncpy(request->request_id, envelope.request_id, sizeof(request->request_id) - 1U);
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
            size->valuedouble < 1 || size->valuedouble > 0x36000 ||
            size->valuedouble != (double)(uint32_t)size->valuedouble ||
            strlen(crc32->valuestring) != 8U) goto done;
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
    else if (strcmp(action->valuestring, "reset_motion_fault") == 0) request->action = CLOUD_PROTOCOL_ACTION_RESET_MOTION_FAULT;
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
    else if (strcmp(action->valuestring, "get_center_calibration_diagnostics") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_CENTER_CAL_DIAGNOSTICS;
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
    else if (strcmp(action->valuestring, "get_log") == 0) request->action = CLOUD_PROTOCOL_ACTION_GET_LOG;
    else if (strcmp(action->valuestring, "reboot") == 0) request->action = CLOUD_PROTOCOL_ACTION_REBOOT;
    else if (strcmp(action->valuestring, "mqtt_runtime_get") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_GET;
#if PATROL_CHASSIS_MQTT_LAB_TEST
    else if (strcmp(action->valuestring, "mqtt_lab_seed_control") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_LAB_SEED_CONTROL;
    else if (strcmp(action->valuestring, "mqtt_lab_modem_restart") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_LAB_MODEM_RESTART;
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
    else if (strcmp(action->valuestring, "mqtt_runtime_cancel") == 0)
        request->action = CLOUD_PROTOCOL_ACTION_MQTT_RUNTIME_CANCEL;
#endif
    else if (strcmp(action->valuestring, "set_mqtt_psk") == 0)
    {
        if (!ABoxCloudInternalV2_PskCredentials(params, &request->mqtt_psk)) goto done;
        request->action = CLOUD_PROTOCOL_ACTION_SET_MQTT_PSK;
    }
    else if (strcmp(action->valuestring, "set_mqtt") == 0)
    {
        ABoxCloudMqttTlsCandidate candidate;
        if (!ABoxCloudInternalV2_MqttTlsCandidate(params, &candidate) ||
            strlen(candidate.vid) >= sizeof(request->mqtt_vid) ||
            strlen(candidate.host) >= sizeof(request->mqtt_host) ||
            strlen(candidate.username) >= sizeof(request->mqtt_username) ||
            strlen(candidate.password) >= sizeof(request->mqtt_password)) goto done;
        strcpy(request->mqtt_vid, candidate.vid);
        strcpy(request->mqtt_host, candidate.host);
        strcpy(request->mqtt_username, candidate.username);
        strcpy(request->mqtt_password, candidate.password);
        request->mqtt_port = candidate.port;
        request->mqtt_tls_enabled = 1U;
        request->mqtt_tls_profile_id = candidate.tls_profile_id;
        request->action = CLOUD_PROTOCOL_ACTION_SET_MQTT;
    }
    else { status = CLOUD_PROTOCOL_UNKNOWN_ACTION; goto done; }
    status = CLOUD_PROTOCOL_OK;

done:
    ABoxCloudInternalV2_Close(&envelope);
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
