#ifndef PATROL_CHASSIS_REMOTE_SERVICE_H
#define PATROL_CHASSIS_REMOTE_SERVICE_H

#include <stdint.h>

#include "chassis_control.h"

void RemoteService_Revoke(void);
/* Fail-closed network boundary. No authorization, session identity, enable
 * request or motion target may survive an MQTT runtime switch. */
void RemoteService_RevokeForNetworkChange(void);
uint8_t RemoteService_FormatNetworkState(char *output, uint16_t size);
#if PATROL_CHASSIS_MQTT_LAB_TEST
void RemoteService_LabSeedNetworkState(void);
#endif
void RemoteService_ExecutionFault(void);
void RemoteService_Init(void);
void RemoteService_Task(void);
void RemoteService_SafetyTick(uint32_t now_ms);
void RemoteService_UpdateFeedback(const ChassisFeedback* feedback);
/* Local transports share the same owner, safety checks and lease watchdog.
 * No network clock or JSON envelope is required on the physical CAN port. */
uint8_t RemoteService_ApplyLocal(const char* action, const char* session_id,
    uint32_t seq, uint32_t lease_ms, int16_t rpm, int32_t steering_mdeg, uint32_t now_ms);
uint8_t RemoteService_HandleControl(const char* payload, uint16_t length, char* response, uint16_t size);
const ChassisControl* RemoteService_State(void);

#endif
