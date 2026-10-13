#ifndef CHASSIS_CLOUD_PROTOCOL_H
#define CHASSIS_CLOUD_PROTOCOL_H

#include <stdint.h>

#define CHASSIS_PUBLIC_PROFILE "vehicle_chassis"

typedef enum {
    CHASSIS_CLOUD_OK = 0,
    CHASSIS_CLOUD_INVALID = 4001,
    CHASSIS_CLOUD_UNSUPPORTED = 4002,
    CHASSIS_CLOUD_REQUEST_EXPIRED = 4005,
    CHASSIS_CLOUD_REQUEST_ID_CONFLICT = 4006
} ChassisCloudStatus;

typedef enum {
    CHASSIS_CLOUD_COMMAND_NONE = 0,
    CHASSIS_CLOUD_COMMAND_MOTION,
    CHASSIS_CLOUD_COMMAND_POWER_ON,
    CHASSIS_CLOUD_COMMAND_POWER_OFF
} ChassisCloudCommandType;

typedef struct {
    ChassisCloudCommandType type;
    char request_id[65]; /* 64 wire characters plus NUL. */
    char action[8];
    char session_id[40];
    uint64_t timestamp_ms;
    uint64_t expires_at;
    uint8_t has_expires_at;
    uint32_t seq;
    uint32_t lease_ms;
    int16_t drive_rpm;
    int32_t steering_mdeg;
} ChassisCloudCommand;

ChassisCloudStatus ChassisCloud_Parse(const char* payload, uint16_t length,
                                      ChassisCloudCommand* command);

#endif
