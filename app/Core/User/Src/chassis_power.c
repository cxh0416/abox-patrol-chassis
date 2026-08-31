#include "chassis_power.h"

void ChassisPower_Init(ChassisPower* power, uint8_t hardware_available, uint32_t now_ms)
{
    power->state = CHASSIS_POWER_OFF;
    power->entered_ms = now_ms;
    power->hardware_available = hardware_available ? 1U : 0U;
    power->relay_request = 0U;
    power->enable_request = 0U;
    power->fault_latched = 0U;
}

uint8_t ChassisPower_RequestOn(ChassisPower* power, uint32_t now_ms)
{
    if (power->state != CHASSIS_POWER_OFF || power->hardware_available == 0U || power->fault_latched) return 0U;
    power->relay_request = 1U;
    power->state = CHASSIS_POWER_POWERING_ON;
    power->entered_ms = now_ms;
    return 1U;
}

void ChassisPower_RequestOff(ChassisPower* power, uint32_t now_ms)
{
    if (power->state == CHASSIS_POWER_OFF) return;
    power->enable_request = 0U;
    power->state = CHASSIS_POWER_STOPPING;
    power->entered_ms = now_ms;
}

void ChassisPower_Tick(ChassisPower* power, uint32_t now_ms, uint8_t contactor_feedback,
                       uint8_t stopped_feedback)
{
    uint32_t elapsed = now_ms - power->entered_ms;
    switch (power->state) {
    case CHASSIS_POWER_POWERING_ON:
        if (contactor_feedback) { power->enable_request = 1U; power->state = CHASSIS_POWER_READY; power->entered_ms = now_ms; }
        else if (elapsed >= 2000U) { power->relay_request = 0U; power->fault_latched = 1U; power->state = CHASSIS_POWER_FAULT; }
        break;
    case CHASSIS_POWER_STOPPING:
        if (stopped_feedback) { power->state = CHASSIS_POWER_POWERING_OFF; power->entered_ms = now_ms; }
        else if (elapsed >= 2000U) { power->relay_request = 0U; power->fault_latched = 1U; power->state = CHASSIS_POWER_FAULT; }
        break;
    case CHASSIS_POWER_POWERING_OFF:
        power->relay_request = 0U;
        if (!contactor_feedback) { power->state = CHASSIS_POWER_OFF; power->entered_ms = now_ms; }
        else if (elapsed >= 2000U) { power->fault_latched = 1U; power->state = CHASSIS_POWER_FAULT; }
        break;
    default:
        break;
    }
}

