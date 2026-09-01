#include "cloud_protocol.h"

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
            size->valuedouble < 1 || size->valuedouble > 0x36800 || strlen(crc32->valuestring) != 8U) goto done;
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
    else if (strcmp(action->valuestring, "get_log") == 0) request->action = CLOUD_PROTOCOL_ACTION_GET_LOG;
    else if (strcmp(action->valuestring, "reboot") == 0) request->action = CLOUD_PROTOCOL_ACTION_REBOOT;
    else if (strcmp(action->valuestring, "power_on") == 0) request->action = CLOUD_PROTOCOL_ACTION_POWER_ON;
    else if (strcmp(action->valuestring, "power_off") == 0) request->action = CLOUD_PROTOCOL_ACTION_POWER_OFF;
    else if (strcmp(action->valuestring, "set_mqtt") == 0)
    {
        mqtt_vid = cJSON_GetObjectItemCaseSensitive(params, "vid");
        mqtt_host = cJSON_GetObjectItemCaseSensitive(params, "host");
        mqtt_port = cJSON_GetObjectItemCaseSensitive(params, "port");
        mqtt_username = cJSON_GetObjectItemCaseSensitive(params, "username");
        mqtt_password = cJSON_GetObjectItemCaseSensitive(params, "password");
        if (!cJSON_IsString(mqtt_vid) || !cJSON_IsString(mqtt_host) || !cJSON_IsNumber(mqtt_port) ||
            !cJSON_IsString(mqtt_username) || !cJSON_IsString(mqtt_password) ||
            !config_string_valid(mqtt_vid->valuestring, sizeof(request->mqtt_vid), 1U) ||
            !config_string_valid(mqtt_host->valuestring, sizeof(request->mqtt_host), 0U) ||
            !config_string_valid(mqtt_username->valuestring, sizeof(request->mqtt_username), 0U) ||
            !config_string_valid(mqtt_password->valuestring, sizeof(request->mqtt_password), 0U) ||
            mqtt_port->valuedouble < 1 || mqtt_port->valuedouble > 65535) goto done;
        strncpy(request->mqtt_vid, mqtt_vid->valuestring, sizeof(request->mqtt_vid) - 1U);
        strncpy(request->mqtt_host, mqtt_host->valuestring, sizeof(request->mqtt_host) - 1U);
        strncpy(request->mqtt_username, mqtt_username->valuestring, sizeof(request->mqtt_username) - 1U);
        strncpy(request->mqtt_password, mqtt_password->valuestring, sizeof(request->mqtt_password) - 1U);
        request->mqtt_port = (uint16_t)mqtt_port->valuedouble;
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
