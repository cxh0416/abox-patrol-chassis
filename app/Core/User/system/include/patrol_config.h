#ifndef PATROL_CHASSIS_CONFIG_H
#define PATROL_CHASSIS_CONFIG_H

#include <stdint.h>

uint8_t PatrolConfig_CalibrationCompatible(void);
uint8_t PatrolConfig_SingleTurnProfileReady(void);

#include "ota_info.h"

#define PATROL_CONFIG_DEFAULT_VID "PATROL-CHASSIS-000"
#define PATROL_CONFIG_DEFAULT_MQTT_HOST ""
#define PATROL_CONFIG_DEFAULT_MQTT_PORT 0U
#ifndef PATROL_CONFIG_DEFAULT_MQTT_USER
#define PATROL_CONFIG_DEFAULT_MQTT_USER ""
#endif
#ifndef PATROL_CONFIG_DEFAULT_MQTT_PASS
#define PATROL_CONFIG_DEFAULT_MQTT_PASS ""
#endif
#define PATROL_CONFIG_DEFAULT_DIAG_HOST "36.153.162.171"
#define PATROL_CONFIG_DEFAULT_DIAG_PORT 30080U
#define PATROL_CONFIG_DEFAULT_DIAG_LEVEL 2U
#define PATROL_CONFIG_DEFAULT_OTA_ORIGIN "https://ota.zxwl.ntiov.com:20443"

void PatrolConfig_ResetDefault(PatrolRemoteConfigV5_t* cfg);
void PatrolConfig_Load(void);
const PatrolRemoteConfigV5_t* PatrolConfig_Get(void);
uint8_t PatrolConfig_IsVidValid(const char* vid);
const ABoxTlsCredentials* PatrolConfig_MqttCredentials(void);
/* Write/readback without changing the immutable RAM stable snapshot. */
uint8_t PatrolConfig_WriteMqttSnapshot(const PatrolRemoteConfigV5_t*, const ABoxTlsCredentials*);
uint8_t PatrolConfig_Save(const PatrolRemoteConfigV5_t* cfg);
uint8_t PatrolConfig_SetMqtt(const char* vid, const char* host, uint16_t port, const char* username,
                          const char* password);
uint8_t PatrolConfig_SetDiagLog(uint8_t enabled, const char* host, uint16_t port, uint8_t level);
uint8_t PatrolConfig_IsCloudReady(const PatrolRemoteConfigV5_t* cfg);
uint8_t PatrolConfig_IsOtaOriginValid(const PatrolRemoteConfigV5_t* cfg);
uint8_t PatrolConfig_SetCalibration(int32_t steering_center_count, uint32_t absolute_center_count,
                                 uint32_t steering_counts_per_degree_milli,
                                 uint32_t absolute_counts_per_degree_milli,
                                 int8_t steering_direction, int8_t absolute_direction,
                                 uint16_t tolerance_mdeg);
uint8_t PatrolConfig_ClearCalibration(void);

#endif
