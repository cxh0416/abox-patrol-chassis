#ifndef TEST_MQTT_RUNTIME_H
#define TEST_MQTT_RUNTIME_H
#include "abox_mqtt_runtime.h"
typedef ABoxMqttRuntimeState MqttRuntimeState;
#define MQTT_RUNTIME_FAILED ABOX_MQTT_RUNTIME_FAILED
int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *);
int MqttRuntime_ActivateStagedCandidate(uint32_t);
int MqttRuntime_StopFirst(void);
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *, uint32_t);
int MqttRuntime_IsReady(void);
MqttRuntimeState MqttRuntime_State(void);
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void);
const ABoxMqttConfig *MqttRuntime_StagedConfig(void);
void MqttRuntime_BlockForRestoreFailure(void);
#endif
