#ifndef PATROL_CHASSIS_REMOTE_SERVICE_H
#define PATROL_CHASSIS_REMOTE_SERVICE_H

#include <stdint.h>

#include "chassis_control.h"

void RemoteService_Init(void);
void RemoteService_Task(void);
void RemoteService_SafetyTick(uint32_t now_ms);
uint8_t RemoteService_HandleControl(const char* payload, uint16_t length, char* response, uint16_t size);
const ChassisControl* RemoteService_State(void);

#endif
