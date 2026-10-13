#ifndef PATROL_MQTT_TRIAL_MANAGER_H
#define PATROL_MQTT_TRIAL_MANAGER_H
#include <stdint.h>
#include "abox_mqtt_trial.h"
void MqttTrialManager_Init(void);
int MqttTrialManager_Stage(const ABoxMqttConfig *candidate, uint64_t proof_id, uint32_t now);
void MqttTrialManager_Accepted(uint32_t now);
void MqttTrialManager_Poll(uint32_t now);
void MqttTrialManager_Cancel(uint32_t now);
void MqttTrialManager_DeferCancel(void);
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState state, uint32_t now);
void MqttTrialManager_OnHeartbeatProof(uint32_t publish_sequence, uint32_t now);
ABoxMqttTrialState MqttTrialManager_State(void);
uint8_t MqttTrialManager_RuntimeRecoveryAllowed(void);
uint64_t MqttTrialManager_Session(void);
int MqttTrialManager_StartEnrollment(const ABoxMqttConfig *candidate, const char *vid, uint32_t now);
#endif
