#ifndef CHASSIS_POWER_H
#define CHASSIS_POWER_H

#include <stdint.h>

typedef enum {
    CHASSIS_POWER_OFF = 0,
    CHASSIS_POWER_POWERING_ON,
    CHASSIS_POWER_READY,
    CHASSIS_POWER_STOPPING,
    CHASSIS_POWER_POWERING_OFF,
    CHASSIS_POWER_FAULT
} ChassisPowerState;

typedef struct {
    ChassisPowerState state;
    uint32_t entered_ms;
    uint8_t hardware_available;
    uint8_t relay_request;
    uint8_t enable_request;
    uint8_t fault_latched;
} ChassisPower;

void ChassisPower_Init(ChassisPower* power, uint8_t hardware_available, uint32_t now_ms);
uint8_t ChassisPower_RequestOn(ChassisPower* power, uint32_t now_ms);
void ChassisPower_RequestOff(ChassisPower* power, uint32_t now_ms);
void ChassisPower_Tick(ChassisPower* power, uint32_t now_ms, uint8_t contactor_feedback,
                       uint8_t stopped_feedback);

#endif

