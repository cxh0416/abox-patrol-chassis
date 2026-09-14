#include "app_test.h"

#include "app_log.h"
#include "FreeRTOS.h"
#include "task.h"

#ifndef PATROL_CHASSIS_APP_TEST_FAULT_KIND
#define PATROL_CHASSIS_APP_TEST_FAULT_KIND 0
#endif

static uint8_t g_triggered;

uint8_t AppTest_WatchdogStep(void)
{
    if (g_triggered) return (PATROL_CHASSIS_APP_TEST_FAULT_KIND == 2) ? 1U : 0U;
    if (xTaskGetTickCount() < pdMS_TO_TICKS(1000U)) return 0U;
    g_triggered = 1U;
    APP_LOGI("OTA_TEST", "testBuild=1 fault=%s", PATROL_CHASSIS_APP_TEST_FAULT_KIND == 1 ? "HARDFAULT" : "IWDG");
#if PATROL_CHASSIS_APP_TEST_FAULT_KIND == 1
    {
        volatile uint32_t *invalid = (volatile uint32_t *)0xFFFFFFF0U;
        *invalid = 0xDEADFA11U;
    }
    return 0U;
#elif PATROL_CHASSIS_APP_TEST_FAULT_KIND == 2
    /* Stop refreshing IWDG; the Boot will classify the resulting reset. */
    return 1U;
#else
    return 0U;
#endif
}
