#ifndef ABOX_PRODUCT_CONFIG_H
#define ABOX_PRODUCT_CONFIG_H

#include "abox_platform_config.h"

#define ABOX_PRODUCT_BUS_UART_BAUD 19200U
#define ABOX_PRODUCT_MODEM_UART_BAUD 115200U
/* Both boards use an 8 MHz crystal. APB1 must stay at or below half HCLK. */
#ifdef ABOX_MCU_GD32F105
#define ABOX_PRODUCT_CLOCK_CONFIG {ABOX_MCU_CLOCK_PLL_HSE, 8000000U, 2U, 27U, 1U, 2U, 1U, 0U, 0U}
#else
#define ABOX_PRODUCT_CLOCK_CONFIG {ABOX_MCU_CLOCK_PLL_HSE, 8000000U, 1U, 9U, 1U, 2U, 1U, 0U, 0U}
#endif

static const ABoxProductConfig ABOX_PRODUCT_CONFIG = {
    "patrol_vehicle_chassis", 0x08000000U, 0x40000U, 0x08000000U, 0x8000U,
    0x08008000U, 0x0803F800U, 0x800U,
    ABOX_SCHEDULER_FREERTOS
};

#endif
