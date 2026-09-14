#ifndef PATROL_CHASSIS_MQTT_QMTRECV_H
#define PATROL_CHASSIS_MQTT_QMTRECV_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t topic_length;
    size_t payload_length;
    uint8_t complete;
    uint8_t outer_escaped;
} MqttQmtRecvFrame_t;

uint8_t MqttQmtRecv_DecodeLine(const char *line,
                               char *topic, size_t topic_capacity,
                               char *payload, size_t payload_capacity,
                               MqttQmtRecvFrame_t *frame);

#endif
