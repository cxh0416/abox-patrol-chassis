#ifndef PATROL_CHASSIS_VCU_CONFIG_H
#define PATROL_CHASSIS_VCU_CONFIG_H

#include <stdint.h>

#include "ota_info.h"

#define VCU_CONFIG_DEFAULT_VID "PATROL-CHASSIS-000"
#define VCU_CONFIG_DEFAULT_MQTT_HOST "36.153.162.171"
#define VCU_CONFIG_DEFAULT_MQTT_PORT 19683U
#ifndef VCU_CONFIG_DEFAULT_MQTT_USER
#define VCU_CONFIG_DEFAULT_MQTT_USER ""
#endif
#ifndef VCU_CONFIG_DEFAULT_MQTT_PASS
#define VCU_CONFIG_DEFAULT_MQTT_PASS ""
#endif
#define VCU_CONFIG_DEFAULT_DIAG_HOST "36.153.162.171"
#define VCU_CONFIG_DEFAULT_DIAG_PORT 30080U
#define VCU_CONFIG_DEFAULT_DIAG_LEVEL 2U
#define VCU_CONFIG_DEFAULT_OTA_ORIGIN "https://ota.zxwl.ntiov.com:20443"

void VcuConfig_ResetDefault(VcuRemoteConfig_t* cfg);
void VcuConfig_Load(void);
const VcuRemoteConfig_t* VcuConfig_Get(void);
uint8_t VcuConfig_Save(const VcuRemoteConfig_t* cfg);
uint8_t VcuConfig_SetRemoteEnabled(uint8_t enabled);
uint8_t VcuConfig_SetMqtt(const char* vid, const char* host, uint16_t port, const char* username,
                          const char* password);
uint8_t VcuConfig_SetDiagLog(uint8_t enabled, const char* host, uint16_t port, uint8_t level);
uint8_t VcuConfig_IsCloudReady(const VcuRemoteConfig_t* cfg);
uint8_t VcuConfig_IsOtaOriginValid(const VcuRemoteConfig_t* cfg);
uint8_t VcuConfig_SetCalibration(int32_t steering_center_count, uint32_t absolute_center_count,
                                 uint32_t absolute_counts_per_degree_milli,
                                 int8_t steering_direction, int8_t absolute_direction,
                                 uint16_t tolerance_mdeg);

#endif
