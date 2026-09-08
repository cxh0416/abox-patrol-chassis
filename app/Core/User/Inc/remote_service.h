#ifndef PATROL_CHASSIS_REMOTE_SERVICE_H
#define PATROL_CHASSIS_REMOTE_SERVICE_H

#include <stdint.h>

#include "chassis_control.h"

uint8_t RemoteService_BenchArm(uint16_t limit_mdeg, uint32_t now);
void RemoteService_BenchRevoke(void);
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
