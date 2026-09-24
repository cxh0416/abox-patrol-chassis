#ifndef PATROL_MQTT_RUNTIME_H
#define PATROL_MQTT_RUNTIME_H

#include <stdint.h>
#include "abox_mqtt_runtime.h"

#define PATROL_MQTT_TLS_CONTEXT 2U
#define PATROL_MQTT_TLS_PROFILE_DEFAULT 0U

typedef ABoxMqttRuntimeState MqttRuntimeState;
#define MQTT_RUNTIME_IDLE ABOX_MQTT_RUNTIME_IDLE
#define MQTT_RUNTIME_DISCONNECT ABOX_MQTT_RUNTIME_DISCONNECT
#define MQTT_RUNTIME_UNBIND ABOX_MQTT_RUNTIME_UNBIND
#define MQTT_RUNTIME_WAIT_CA ABOX_MQTT_RUNTIME_WAIT_CA
#define MQTT_RUNTIME_CLOCK ABOX_MQTT_RUNTIME_CLOCK
#define MQTT_RUNTIME_PREPARE ABOX_MQTT_RUNTIME_PREPARE
#define MQTT_RUNTIME_BIND ABOX_MQTT_RUNTIME_BIND
#define MQTT_RUNTIME_PREPARED ABOX_MQTT_RUNTIME_PREPARED
#define MQTT_RUNTIME_CONNECT ABOX_MQTT_RUNTIME_CONNECT
#define MQTT_RUNTIME_CONNECTED ABOX_MQTT_RUNTIME_CONNECTED
#define MQTT_RUNTIME_READY ABOX_MQTT_RUNTIME_READY
#define MQTT_RUNTIME_FAILED ABOX_MQTT_RUNTIME_FAILED

int MqttRuntime_Init(ABoxEc800At *at, const ABoxMqttConfig *config, uint32_t now);
void MqttRuntime_Task(uint32_t now);
/* RAM-only 7.1 entry. Caller credentials are copied; no Flash API is called. */
int MqttRuntime_ApplyRamCandidate(const ABoxMqttConfig *config, uint32_t now);
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *config, uint32_t now);
void MqttRuntime_BlockForRestoreFailure(void);
int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *config);
int MqttRuntime_ActivateStagedCandidate(uint32_t now);
int MqttRuntime_IsReady(void);
int MqttRuntime_StopFirst(void);
MqttRuntimeState MqttRuntime_State(void);
uint8_t MqttRuntime_ActiveClient(void);
uint8_t MqttRuntime_ActiveTls(void);
uint8_t MqttRuntime_ActiveContext(void);
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void);
const ABoxMqttConfig *MqttRuntime_StagedConfig(void);
void MqttRuntime_OnModemReset(void);

#endif
