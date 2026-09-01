#include "vehicle_power_port.h"

#include "app_log.h"
#include "main.h"
#include "vehicle_power_output.h"

static uint8_t write_power_output(uint8_t on)
{
#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
    uint8_t latched_on;
    HAL_GPIO_WritePin(PVIN_OUT1_GPIO_Port, PVIN_OUT1_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    latched_on = (PVIN_OUT1_GPIO_Port->ODR & PVIN_OUT1_Pin) != 0U ? 1U : 0U;
    APP_LOGI("PWR", "vehicle output commanded=%u latched=%u",
             (unsigned int)(on ? 1U : 0U), (unsigned int)latched_on);
    return latched_on == (on ? 1U : 0U) ? 1U : 0U;
#else
    (void)on;
    return 0U;
#endif
}

void VehiclePowerPort_Init(void)
{
#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
    VehiclePowerOutput_Init(write_power_output);
#else
    VehiclePowerOutput_Init(0);
#endif
}
