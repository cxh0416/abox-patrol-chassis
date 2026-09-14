#include "vehicle_power_output.h"

static VehiclePowerOutputWriteFn g_write;
static uint8_t g_commanded_on;

void VehiclePowerOutput_Init(VehiclePowerOutputWriteFn write)
{
    g_write = write;
    g_commanded_on = 0U;
    if (g_write != 0) (void)g_write(0U);
}

uint8_t VehiclePowerOutput_Set(uint8_t on)
{
    uint8_t target = on ? 1U : 0U;
    if (g_write == 0 || !g_write(target)) return 0U;
    g_commanded_on = target;
    return 1U;
}

uint8_t VehiclePowerOutput_IsAvailable(void)
{
    return g_write != 0 ? 1U : 0U;
}

uint8_t VehiclePowerOutput_IsOn(void)
{
    return g_commanded_on;
}
