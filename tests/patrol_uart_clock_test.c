/* Reuse the platform UART peripheral model with each product clock tree. */
#define ABoxF105Uart_ClockHz abox_upstream_test_clock
#define main abox_upstream_uart_test_main
#include "../platform/tests/abox_f105_uart_test.c"
#undef main
#undef ABoxF105Uart_ClockHz
#include "abox_product_config.h"

uint32_t ABoxF105Uart_ClockHz(unsigned id)
{
#ifdef ABOX_MCU_GD32F105
    return id == 1U ? 108000000U : 54000000U;
#else
    return id == 1U ? 72000000U : 36000000U;
#endif
}

static void check_baud(unsigned id, uint32_t requested, uint32_t divisor)
{
    uint64_t actual_scaled = ABoxF105Uart_ClockHz(id);
    uint64_t target_scaled = (uint64_t)requested * divisor;
    uint64_t error = actual_scaled > target_scaled ? actual_scaled - target_scaled : target_scaled - actual_scaled;
    assert(uart[id].baud == divisor);
    assert(error * 1000U < target_scaled); /* less than 0.1 percent */
}

int main(void)
{
    const ABoxUartConfig modem = {ABOX_PRODUCT_MODEM_UART_BAUD, 8U, 1U,
                                  ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode = ABOX_UART_ASYNC};
    const ABoxUartConfig encoder = {ABOX_PRODUCT_BUS_UART_BAUD, 8U, 1U,
                                    ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode = ABOX_UART_ASYNC};
    assert(ABoxUart_Open(1U, &modem) && ABoxUart_IsConfig(1U, &modem));
    assert(ABoxUart_Open(5U, &encoder) && ABoxUart_IsConfig(5U, &encoder));
#ifdef ABOX_MCU_GD32F105
    check_baud(1U, modem.baud, 938U);
    check_baud(5U, encoder.baud, 2813U);
#else
    check_baud(1U, modem.baud, 625U);
    check_baud(5U, encoder.baud, 1875U);
#endif
    return 0;
}
