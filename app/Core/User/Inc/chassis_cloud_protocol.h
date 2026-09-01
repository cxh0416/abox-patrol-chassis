#ifndef CHASSIS_CLOUD_PROTOCOL_H
#define CHASSIS_CLOUD_PROTOCOL_H

#include <stdint.h>

#define CHASSIS_PUBLIC_DEVICE_TYPE "vehicle_chassis"

typedef enum {
    CHASSIS_CLOUD_OK = 0,
    CHASSIS_CLOUD_INVALID = 4001,
    CHASSIS_CLOUD_UNSUPPORTED = 4002,
    CHASSIS_CLOUD_DEVICE_TYPE_MISMATCH = 4004
} ChassisCloudStatus;

typedef struct {
    char request_id[64];
    char action[8];
    char session_id[40];
    uint32_t seq;
    uint32_t lease_ms;
    int16_t drive_rpm;
    int32_t steering_mdeg;
} ChassisCloudCommand;

ChassisCloudStatus ChassisCloud_Parse(const char* payload, uint16_t length,
                                      ChassisCloudCommand* command);

#endif
