#ifndef PATROL_CHASSIS_CLOUD_PROTOCOL_H
#define PATROL_CHASSIS_CLOUD_PROTOCOL_H

#include <stdint.h>

#define CLOUD_PROTOCOL_REQUEST_ID_SIZE 64U
#define CLOUD_PROTOCOL_MAX_PAYLOAD 1023U
#define CLOUD_PROTOCOL_OTA_URL_SIZE 321U
#define CLOUD_PROTOCOL_OTA_VERSION_SIZE 32U
#define CLOUD_PROTOCOL_MQTT_HOST_SIZE 64U
#define CLOUD_PROTOCOL_MQTT_USER_SIZE 32U
#define CLOUD_PROTOCOL_MQTT_PASSWORD_SIZE 64U

typedef enum {
    CLOUD_PROTOCOL_ACTION_NONE = 0,
    CLOUD_PROTOCOL_ACTION_GET_INFO,
    CLOUD_PROTOCOL_ACTION_GET_LOG,
    CLOUD_PROTOCOL_ACTION_REBOOT,
    CLOUD_PROTOCOL_ACTION_POWER_ON,
    CLOUD_PROTOCOL_ACTION_POWER_OFF,
    CLOUD_PROTOCOL_ACTION_SET_MQTT,
    CLOUD_PROTOCOL_ACTION_OTA,
    CLOUD_PROTOCOL_ACTION_GET_CAN_DIAG,
    CLOUD_PROTOCOL_ACTION_GET_CALIBRATION,
    CLOUD_PROTOCOL_ACTION_SET_CALIBRATION,
    CLOUD_PROTOCOL_ACTION_CLEAR_CALIBRATION,
    CLOUD_PROTOCOL_ACTION_BENCH_ARM,
    CLOUD_PROTOCOL_ACTION_BENCH_STOP,
    CLOUD_PROTOCOL_ACTION_BENCH_GET,
} CloudProtocolAction_t;

typedef enum {
    CLOUD_PROTOCOL_OK = 0,
    CLOUD_PROTOCOL_INVALID = 400,
    CLOUD_PROTOCOL_UNKNOWN_ACTION = 404,
    CLOUD_PROTOCOL_TOO_LARGE = 413,
} CloudProtocolStatus_t;

typedef struct {
    char request_id[CLOUD_PROTOCOL_REQUEST_ID_SIZE];
    CloudProtocolAction_t action;
    char ota_url[CLOUD_PROTOCOL_OTA_URL_SIZE];
    char ota_version[CLOUD_PROTOCOL_OTA_VERSION_SIZE];
    uint32_t ota_size;
    uint32_t ota_crc32;
    char mqtt_vid[32];
    char mqtt_host[CLOUD_PROTOCOL_MQTT_HOST_SIZE];
    uint16_t mqtt_port;
    char mqtt_username[CLOUD_PROTOCOL_MQTT_USER_SIZE];
    char mqtt_password[CLOUD_PROTOCOL_MQTT_PASSWORD_SIZE];
    uint16_t bench_limit_mdeg;
    int32_t steering_center_count;
    uint32_t absolute_center_count;
    uint32_t steering_counts_per_degree_milli;
    uint32_t absolute_counts_per_degree_milli;
    uint16_t steering_tolerance_mdeg;
    int8_t steering_direction;
    int8_t absolute_direction;
} CloudProtocolRequest_t;

CloudProtocolStatus_t CloudProtocol_Parse(const char* payload, uint16_t length, uint8_t ota_topic,
                                          CloudProtocolRequest_t* request);
const char* CloudProtocol_StatusMessage(CloudProtocolStatus_t status);

#endif
