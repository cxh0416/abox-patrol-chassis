#include <assert.h>
#include <stdint.h>

#include "vehicle_power_output.h"

static uint8_t commanded;
static uint8_t write_output(uint8_t on)
{
    commanded = on;
    return 1U;
}

int main(void)
{
    VehiclePowerOutput_Init(write_output);
    assert(commanded == 0U);
    assert(VehiclePowerOutput_Set(1U) == 0U);
    assert(commanded == 0U);
    assert(VehiclePowerOutput_IsOn() == 0U);
    assert(VehiclePowerOutput_Set(0U) == 1U);
    return 0;
}
