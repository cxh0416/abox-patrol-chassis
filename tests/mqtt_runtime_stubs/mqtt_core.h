#ifndef TEST_MQTT_CORE_H
#define TEST_MQTT_CORE_H
#include <stdint.h>
#include "abox_mqtt_trial.h"
uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig *config);
void MQTT_Core_SetPaused(uint8_t paused);
void MQTT_Core_SetSecurityReady(uint8_t ready);
void MQTT_Core_Task(void);
uint8_t MQTT_Core_IsReady(void);
typedef enum { MQTT_CORE_ST_SUB, MQTT_CORE_ST_WAIT_SUB, MQTT_CORE_ST_READY } MQTT_CoreState_t;
MQTT_CoreState_t MQTT_Core_GetState(void);
#endif
