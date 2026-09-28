#ifndef PATROL_CHASSIS_REMOTE_SERVICE_H
#define PATROL_CHASSIS_REMOTE_SERVICE_H

#include <stdint.h>

#include "chassis_control.h"

uint8_t RemoteService_BenchArm(uint16_t limit_mdeg, const char* request_id, uint32_t now);
uint8_t RemoteService_BenchDriveArm(uint16_t limit_mdeg, const char* request_id, uint32_t now);
uint8_t RemoteService_BenchDrivePermitted(void);
void RemoteService_BenchRevoke(void);
/* Fail-closed network boundary. No authorization, session identity, enable
 * request or motion target may survive an MQTT runtime switch. */
void RemoteService_RevokeForNetworkChange(void);
uint8_t RemoteService_FormatNetworkState(char *output, uint16_t size);
#if PATROL_CHASSIS_MQTT_LAB_TEST
void RemoteService_LabSeedNetworkState(void);
#endif
void RemoteService_BenchFault(void);
uint8_t RemoteService_BenchPermitted(void);
uint16_t RemoteService_BenchLimit(void);
const char* RemoteService_BenchSession(void);
void RemoteService_Init(void);
void RemoteService_Task(void);
void RemoteService_SafetyTick(uint32_t now_ms);
void RemoteService_UpdateFeedback(const ChassisFeedback* feedback);
uint8_t RemoteService_HandleControl(const char* payload, uint16_t length, char* response, uint16_t size);
const ChassisControl* RemoteService_State(void);

#endif
