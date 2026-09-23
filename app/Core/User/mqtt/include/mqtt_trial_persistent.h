#ifndef PATROL_MQTT_TRIAL_PERSISTENT_H
#define PATROL_MQTT_TRIAL_PERSISTENT_H
#include <stdint.h>
#include "patrol_config.h"
int MqttTrialManager_StagePersistent(const PatrolRemoteConfigV5_t *candidate, uint64_t proof_id, uint32_t now);
#endif
