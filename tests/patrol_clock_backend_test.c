/* Exercise the product's actual clock configuration through the platform
 * calculation and register-backend model for each MCU. */
#define main abox_upstream_clock_test_main
#include "../platform/tests/abox_clock_backend_test.c"
#undef main
#include "abox_product_config.h"

int main(void)
{
    static const ABoxMcuClockConfig config = ABOX_PRODUCT_CLOCK_CONFIG;
    ABoxMcuClockFrequencies frequencies;

#ifdef ABOX_MCU_GD32F105
    system_hz = ahb_hz = apb2_hz = 108000000U;
    apb1_hz = 54000000U;
#else
    system_hz = ahb_hz = apb2_hz = 72000000U;
    apb1_hz = 36000000U;
    (void)clock_source; (void)pll_ready; (void)fail_latency;
#endif
    assert(ABoxMcuClock_Calculate(&config, &frequencies) == ABOX_HAL_OK);
    assert(frequencies.system_hz == system_hz && frequencies.ahb_hz == ahb_hz);
    assert(frequencies.apb1_hz == apb1_hz && frequencies.apb2_hz == apb2_hz);
    assert(frequencies.apb1_timer_hz == ahb_hz && frequencies.apb2_timer_hz == ahb_hz);
    assert(ABoxMcuClock_Apply(&config) == ABOX_HAL_OK);
#ifdef ABOX_MCU_GD32F105
    assert(pll_mul == RCU_PLL_MUL27 && prediv == 1U);
    assert(aux_source == RCU_PREDV0SRC_HXTAL);
    assert(SystemCoreClock == 108000000U);
#else
    assert(pll_mul == RCC_PLL_MUL9 && prediv == RCC_HSE_PREDIV_DIV1);
#endif
    assert(apb1_div == 2U && latency_seen == 2U);
    return 0;
}
