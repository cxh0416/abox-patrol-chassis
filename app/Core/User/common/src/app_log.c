#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "patrol_diag_config.h"
#include "main.h"
#include "time_base.h"
#if PATROL_EC800_AT_DIAG_UART5 || defined(PATROL_OTA_FIELD_PROGRESS_UART5)
#include "usart.h"
#endif

#define APP_LOG_LINE_COUNT 32U
#define APP_LOG_LINE_SIZE 80U

static char g_log_lines[APP_LOG_LINE_COUNT][APP_LOG_LINE_SIZE];
static uint32_t g_log_ticks[APP_LOG_LINE_COUNT];
static uint32_t g_log_sequences[APP_LOG_LINE_COUNT];
static AppLogLevel_t g_log_levels[APP_LOG_LINE_COUNT];
static uint16_t g_log_wr = 0U;
static uint16_t g_log_count = 0U;
static uint16_t g_log_mqtt_rd = 0U;
static uint32_t g_log_next_sequence = 1U;
static uint32_t g_log_mqtt_sequence = 0U;
static uint32_t g_log_mqtt_dropped = 0U;
#if PATROL_EC800_AT_DIAG_UART5
static uint16_t g_log_debug_rd = 0U;
#endif
static AppLogLevel_t g_log_level = APP_LOG_LEVEL_INFO;

static const char* level_name(AppLogLevel_t level)
{
    switch (level)
    {
        case APP_LOG_LEVEL_ERROR: return "E";
        case APP_LOG_LEVEL_WARN: return "W";
        case APP_LOG_LEVEL_INFO: return "I";
        case APP_LOG_LEVEL_DEBUG:
        default: return "D";
    }
}

static uint8_t AppLog_IsDumpNoise(const char* line)
{
    if (line == 0) return 1U;
    if (strstr(line, " AT TX owner=") != 0) return 1U;
    if (strstr(line, " AT RX owner=") != 0) return 1U;
    if (strstr(line, " AT PAY owner=") != 0) return 1U;
    if (strstr(line, " MQTT heartbeat publish busy") != 0) return 1U;
    if (strstr(line, " MQTT payload owner mismatch") != 0) return 1U;
    return 0U;
}

static uint16_t AppLog_FormatLine(uint16_t idx, char* out, uint16_t out_sz)
{
    char ts[24];
    uint32_t tick;
    int n;

    if ((out == 0) || (out_sz == 0U)) return 0U;
    out[0] = '\0';
    idx = (uint16_t)(idx % APP_LOG_LINE_COUNT);
    tick = g_log_ticks[idx];

    if (!TimeBase_FormatBeijingFromTick(tick, ts, sizeof(ts)))
    {
        snprintf(ts, sizeof(ts), "BOOT+%lu.%03lus", (unsigned long)(tick / 1000U), (unsigned long)(tick % 1000U));
    }

    n = snprintf(out, out_sz, "[%s] %s", ts, g_log_lines[idx]);
    if (n < 0) return 0U;
    if (n >= out_sz) return (uint16_t)(out_sz - 1U);
    return (uint16_t)n;
}

void AppLog_Init(void)
{
    AppLog_Clear();
    g_log_level = APP_LOG_LEVEL_INFO;
}

void AppLog_Clear(void)
{
    memset(g_log_lines, 0, sizeof(g_log_lines));
    memset(g_log_ticks, 0, sizeof(g_log_ticks));
    memset(g_log_sequences, 0, sizeof(g_log_sequences));
    memset(g_log_levels, 0, sizeof(g_log_levels));
    g_log_wr = 0U;
    g_log_count = 0U;
    g_log_mqtt_rd = 0U;
    g_log_next_sequence = 1U;
    g_log_mqtt_sequence = 0U;
    g_log_mqtt_dropped = 0U;
#if PATROL_EC800_AT_DIAG_UART5
    g_log_debug_rd = 0U;
#endif
}

void AppLog_SetLevel(AppLogLevel_t level) { g_log_level = level; }
AppLogLevel_t AppLog_GetLevel(void) { return g_log_level; }

void AppLog_FieldStartup(const char* version)
{
#ifdef PATROL_OTA_FIELD_PROGRESS_UART5
    char line[128];
    int n = snprintf(line, sizeof(line), "[APP2] evt=APP_START version=%s fieldBuild=1 role=OTA_OBSERVE\r\n",
                     version == 0 ? "unknown" : version);
    if (n > 0) (void)HAL_UART_Transmit(&huart5, (uint8_t*)line, (uint16_t)n, 100U);
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
    if (n > 0) (void)HAL_UART_Transmit(&huart5, (uint8_t*)line, (uint16_t)n, 100U);
#else
    (void)threshold_percent;
    (void)actual_percent;
    (void)downloaded;
    (void)size;
    (void)slot;
#endif
}

void AppLog_Printf(AppLogLevel_t level, const char* tag, const char* fmt, ...)
{
    char msg[64];
    va_list ap;
    int n;
    uint16_t idx;

    if ((fmt == 0) || (level > g_log_level)) return;

    va_start(ap, fmt);
    n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        idx = g_log_wr;
        g_log_ticks[idx] = HAL_GetTick();
        g_log_levels[idx] = level;
        g_log_sequences[idx] = g_log_next_sequence++;
        snprintf(g_log_lines[idx], APP_LOG_LINE_SIZE, "%s %s %s\r\n", level_name(level), tag ? tag : "APP", msg);
        g_log_wr = (uint16_t)((g_log_wr + 1U) % APP_LOG_LINE_COUNT);
        if (g_log_count < APP_LOG_LINE_COUNT) g_log_count++;
        if (!primask) __enable_irq();
    }
}

uint16_t AppLog_Dump(char* out, uint16_t out_sz)
{
    uint16_t i;
    uint16_t used = 0U;
    uint16_t start;
    uint16_t first = 0U;
    uint16_t lens[APP_LOG_LINE_COUNT];

    if ((out == 0) || (out_sz == 0U)) return 0U;
    out[0] = '\0';
    start = (g_log_wr + APP_LOG_LINE_COUNT - g_log_count) % APP_LOG_LINE_COUNT;

    for (i = 0U; i < g_log_count; i++)
    {
        char line[128];
        lens[i] = AppLog_FormatLine((uint16_t)((start + i) % APP_LOG_LINE_COUNT), line, sizeof(line));
        if (AppLog_IsDumpNoise(line)) lens[i] = 0U;
    }

    first = g_log_count;
    for (i = g_log_count; i > 0U; i--)
    {
        uint16_t n = lens[(uint16_t)(i - 1U)];
        if (n == 0U) continue;
        if ((used + n + 1U) >= out_sz) break;
        used = (uint16_t)(used + n);
        first = (uint16_t)(i - 1U);
    }

    used = 0U;
    for (i = first; i < g_log_count; i++)
    {
        char line[128];
        uint16_t n = lens[i];
        if (n == 0U) continue;

        if ((used + n + 1U) >= out_sz) break;
        (void)AppLog_FormatLine((uint16_t)((start + i) % APP_LOG_LINE_COUNT), line, sizeof(line));
        memcpy(out + used, line, n);
        used += n;
        out[used] = '\0';
    }
    return used;
}

uint16_t AppLog_ReadMqttLine(char* out, uint16_t out_sz)
{
    uint16_t oldest;
    uint16_t newest;
    uint16_t n;

    if ((out == 0) || (out_sz == 0U) || (g_log_count == 0U)) return 0U;

    oldest = (g_log_wr + APP_LOG_LINE_COUNT - g_log_count) % APP_LOG_LINE_COUNT;
    newest = g_log_wr;
    if (g_log_count == APP_LOG_LINE_COUNT)
    {
        uint16_t dist = (g_log_mqtt_rd + APP_LOG_LINE_COUNT - oldest) % APP_LOG_LINE_COUNT;
        if (dist >= g_log_count) g_log_mqtt_rd = oldest;
    }
    if (g_log_mqtt_rd == newest) return 0U;

    do
    {
        n = AppLog_FormatLine(g_log_mqtt_rd, out, out_sz);
        g_log_mqtt_rd = (uint16_t)((g_log_mqtt_rd + 1U) % APP_LOG_LINE_COUNT);
        if (!AppLog_IsDumpNoise(out)) return n;
    } while (g_log_mqtt_rd != newest);

    out[0] = '\0';
    return 0U;
}

uint16_t AppLog_ReadMqttAlert(char* out, uint16_t out_sz, AppLogLevel_t* level)
{
    uint16_t i;
    uint16_t best = APP_LOG_LINE_COUNT;
    uint32_t best_sequence = 0xFFFFFFFFU;
    uint32_t oldest_sequence = 0xFFFFFFFFU;
    uint32_t primask;
    uint16_t n = 0U;

    if ((out == 0) || (out_sz == 0U)) return 0U;
    out[0] = '\0';
    primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < APP_LOG_LINE_COUNT; i++)
    {
        uint32_t seq = g_log_sequences[i];
        if ((seq != 0U) && (seq < oldest_sequence)) oldest_sequence = seq;
        if ((seq > g_log_mqtt_sequence) && (seq < best_sequence))
        {
            best_sequence = seq;
            best = i;
        }
    }
    if ((oldest_sequence != 0xFFFFFFFFU) && (g_log_mqtt_sequence + 1U < oldest_sequence))
        g_log_mqtt_dropped += oldest_sequence - (g_log_mqtt_sequence + 1U);

    while (best < APP_LOG_LINE_COUNT)
    {
        AppLogLevel_t current_level = g_log_levels[best];
        g_log_mqtt_sequence = g_log_sequences[best];
        if (current_level <= APP_LOG_LEVEL_WARN)
        {
            n = AppLog_FormatLine(best, out, out_sz);
            if (level != 0) *level = current_level;
            break;
        }
        best = APP_LOG_LINE_COUNT;
        best_sequence = 0xFFFFFFFFU;
        for (i = 0U; i < APP_LOG_LINE_COUNT; i++)
        {
            uint32_t seq = g_log_sequences[i];
            if ((seq > g_log_mqtt_sequence) && (seq < best_sequence))
            {
                best_sequence = seq;
                best = i;
            }
        }
    }
    if (!primask) __enable_irq();
    return n;
}

uint32_t AppLog_GetMqttDroppedCount(void) { return g_log_mqtt_dropped; }

#if PATROL_EC800_AT_DIAG_UART5
void AppLog_DebugTask(void)
{
    char line[128];
    uint16_t n = 0U;
    uint16_t oldest;
    uint16_t newest;

    /* Format in the main loop; AppLog_Printf may be called from UART IRQ. */
    __disable_irq();
    if (g_log_count > 0U)
    {
        oldest = (g_log_wr + APP_LOG_LINE_COUNT - g_log_count) % APP_LOG_LINE_COUNT;
        newest = g_log_wr;
        if (g_log_count == APP_LOG_LINE_COUNT)
        {
            uint16_t dist = (g_log_debug_rd + APP_LOG_LINE_COUNT - oldest) % APP_LOG_LINE_COUNT;
            if (dist >= g_log_count) g_log_debug_rd = oldest;
        }

        if (g_log_debug_rd != newest)
        {
            n = AppLog_FormatLine(g_log_debug_rd, line, sizeof(line));
            g_log_debug_rd = (uint16_t)((g_log_debug_rd + 1U) % APP_LOG_LINE_COUNT);
        }
    }
    __enable_irq();

    if (n > 0U) { (void)HAL_UART_Transmit(&huart5, (uint8_t*)line, n, 1000U); }
}
#else
void AppLog_DebugTask(void) {}
#endif
