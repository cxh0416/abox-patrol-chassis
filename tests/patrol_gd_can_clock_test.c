/* Reuse the public platform's simulated GD CAN peripherals. */
#define main abox_upstream_can_test_main
#include "../platform/tests/gd32f105_can_test.c"
#undef main
#include "patrol_can_config.h"

int main(void)
{
    static const ABoxCanConfig config = PATROL_CAN_CONFIG;
    clock_hz = 54000000U;
    assert(ABoxGdV1Can_Bind());
    assert(ABoxCanPort_Init(0U, &config));
    assert(last_bus == CAN0 && last_config.prescaler == 6U);
    assert(last_config.time_segment_1 == 14U && last_config.time_segment_2 == 1U);
    assert(ABoxCanPort_Init(1U, &config));
    assert(last_bus == CAN1 && last_config.prescaler == 6U);
    assert(last_config.auto_retrans == ENABLE && last_config.auto_bus_off_recovery == ENABLE);
    assert(ABoxCanPort_Stop(1U) && ABoxCanPort_Stop(0U));
    return 0;
}
