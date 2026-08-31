#ifndef CHASSIS_CLOUD_PROTOCOL_H
#define CHASSIS_CLOUD_PROTOCOL_H

#include <stdint.h>

typedef struct {
    char request_id[64];
    char action[8];
    char session_id[40];
    uint32_t seq;
    uint32_t lease_ms;
    int16_t drive_rpm;
    int32_t steering_mdeg;
} ChassisCloudCommand;

uint8_t ChassisCloud_Parse(const char* payload, uint16_t length, ChassisCloudCommand* command);

#endif

