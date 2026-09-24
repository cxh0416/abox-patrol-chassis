#ifndef PATROL_MQTT_PROFILE_H
#define PATROL_MQTT_PROFILE_H

#include "abox_mqtt_v4.h"

const ABoxMqttProfileDescriptor *PatrolMqttProfile_Get(void);
const ABoxMqttV4Registry *PatrolMqttProfile_Registry(void);

#endif
