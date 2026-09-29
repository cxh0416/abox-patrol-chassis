#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "abox_log_ring.h"
#include "patrol_diag_config.h"
#include "main.h"
#include "time_base.h"
#if PATROL_EC800_AT_DIAG_UART5 || defined(PATROL_OTA_FIELD_PROGRESS_UART5)
#include "usart.h"
#endif

static ABoxLogRing g_log;

static int format_time(uint32_t tick, char *output, uint16_t capacity)
{
    return TimeBase_FormatBeijingFromTick(tick, output, capacity);
}

void AppLog_Init(void)
{
    ABoxLogRing_Init(&g_log);
}

void AppLog_Clear(void)
{
    ABoxLogRing_Clear(&g_log);
}

void AppLog_SetLevel(AppLogLevel_t level)
{
    ABoxLogRing_SetLevel(&g_log, (uint8_t)level);
}

AppLogLevel_t AppLog_GetLevel(void)
{
    return (AppLogLevel_t)ABoxLogRing_GetLevel(&g_log);
}

void AppLog_FieldStartup(const char *version)
{
#ifdef PATROL_OTA_FIELD_PROGRESS_UART5
    char line[128];
    int n = snprintf(line, sizeof(line),
                     "[APP2] evt=APP_START version=%s fieldBuild=1 role=OTA_OBSERVE\r\n",
                     version == 0 ? "unknown" : version);
    if (n > 0) (void)HAL_UART_Transmit(&huart5, (uint8_t *)line, (uint16_t)n, 100U);
#else
    (void)version;
#endif
}

void AppLog_FieldOtaProgress(uint32_t threshold_percent, uint32_t actual_percent,
                             uint32_t downloaded, uint32_t size, uint8_t slot)
{
#ifdef PATROL_OTA_FIELD_PROGRESS_UART5
    char line[160];
    int n = snprintf(line, sizeof(line),
                     "[APP2] evt=OTA_DOWNLOAD_PROGRESS threshold=%lu actualPercent=%lu downloaded=%lu size=%lu slot=%u\r\n",
                     (unsigned long)threshold_percent, (unsigned long)actual_percent,
                     (unsigned long)downloaded, (unsigned long)size, (unsigned)slot);
    if (n > 0) (void)HAL_UART_Transmit(&huart5, (uint8_t *)line, (uint16_t)n, 100U);
#else
    (void)threshold_percent;
    (void)actual_percent;
    (void)downloaded;
    (void)size;
    (void)slot;
#endif
}

void AppLog_Printf(AppLogLevel_t level, const char *tag, const char *format, ...)
{
    char message[64];
    va_list arguments;
    uint32_t primask;
    if (!format || level > AppLog_GetLevel()) return;
    va_start(arguments, format);
    if (vsnprintf(message, sizeof(message), format, arguments) < 0) {
        va_end(arguments);
        return;
    }
    va_end(arguments);
    primask = __get_PRIMASK();
    __disable_irq();
    ABoxLogRing_PushText(&g_log, HAL_GetTick(), (uint8_t)level, tag, message);
    if (!primask) __enable_irq();
}

uint16_t AppLog_Dump(char *output, uint16_t capacity)
{
    return ABoxLogRing_Dump(&g_log, output, capacity, format_time);
}

uint16_t AppLog_ReadMqttLine(char *output, uint16_t capacity)
{
    return ABoxLogRing_ReadMqttLine(&g_log, output, capacity, format_time);
}

uint16_t AppLog_ReadMqttAlert(char *output, uint16_t capacity,
                              AppLogLevel_t *level)
{
    uint8_t value = 0U;
    uint16_t length;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    length = ABoxLogRing_ReadMqttAlert(&g_log, output, capacity,
                                       level ? &value : 0, format_time);
    if (!primask) __enable_irq();
    if (length && level) *level = (AppLogLevel_t)value;
    return length;
}

uint32_t AppLog_GetMqttDroppedCount(void)
{
    return ABoxLogRing_MqttDropped(&g_log);
}

#if PATROL_EC800_AT_DIAG_UART5
void AppLog_DebugTask(void)
{
    char line[128];
    uint16_t length;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    length = ABoxLogRing_ReadDebugLine(&g_log, line, sizeof(line), format_time);
    if (!primask) __enable_irq();
    if (length) (void)HAL_UART_Transmit(&huart5, (uint8_t *)line, length, 1000U);
}
#else
void AppLog_DebugTask(void) {}
#endif
