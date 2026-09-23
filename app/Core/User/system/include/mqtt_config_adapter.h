#ifndef PATROL_MQTT_CONFIG_ADAPTER_H
#define PATROL_MQTT_CONFIG_ADAPTER_H

#include <stdint.h>
#include "abox_mqtt_trial.h"
#include "ota_info.h"

uint8_t PatrolMqttConfig_FromV5(const PatrolRemoteConfigV5_t *config,
                                ABoxMqttConfig *runtime);

#endif
