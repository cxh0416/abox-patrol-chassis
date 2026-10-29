#include "time_base.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "main.h"
#include "abox_platform_port.h"
#include "abox_mqtt_v4.h"

static ABoxClock g_clock ;
static uint8_t g_cclk_mode_known, g_cclk_local;
static uint64_t g_log_mapping_start ;

#define TIMEBASE_BEIJING_OFFSET_SEC (8U * 3600U)

// 按公历规则判断指定年份是否为闰年。处理四年、百年和四百年例外，返回 1 或 0。
static uint8_t is_leap_year(int y)
{
    return (((y % 4) == 0 && (y % 100) != 0) || ((y % 400) == 0)) ? 1U : 0U;
}

// 取得指定年份和月份的天数。二月考虑闰年，非法月份按当前实现兜底返回 31，调用方正常日期运算应提供合法月份。
static uint8_t month_days(int y, int mon)
{
    static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    if ((mon == 2) && is_leap_year(y)) return 29U;
    if ((mon < 1) || (mon > 12)) return 31U;
    return days[mon - 1];
}

// 初始化可信时钟与模块时区模式观察状态。初始无 UTC 来源，日志 tick 到日历时间的可追溯起点在成功引导或同步后建立。
void TimeBase_Init(void)
{
    ABoxClock_Init(&g_clock);
    g_cclk_mode_known = g_cclk_local = 0U;
    g_log_mapping_start = 0U;
}

/*
 * 解析：
 * +CCLK: "25/03/25,11:20:30+32"
 */
// 观察模块时区模式和时钟 AT 行以尝试引导 UTC。RDY 清除旧模式，未知 CTZU 模式拒绝 CCLK 引导，避免把本地时钟误当 UTC。
void TimeBase_UpdateFromCCLK(const char* line)
{
    uint64_t utc, mono;
    int mode;
    if (!line) return;
    if (!strcmp(line, "RDY")) { g_cclk_mode_known = 0U; return; }
    if (sscanf(line, "+CTZU: %d", &mode) == 1) {
        g_cclk_mode_known = (mode == 1 || mode == 3);
        g_cclk_local = mode == 3;
        return;
    }
    if (!strstr(line, "+CCLK:")) return;
    if (!g_cclk_mode_known || !ABoxClock_ParseCCLK(line, g_cclk_local, &utc)) {
        APP_LOGW("TIME", "CCLK unknown mode or invalid");
        return;
    }
    mono = ABox_PortGetMonotonicMs();
    if (ABoxClock_Bootstrap(&g_clock, utc, mono)) g_log_mapping_start = mono;
}

// 读取当前快照中的 UTC 毫秒值。调用者需要可信绝对时间时应同时检查有效来源，不能把该接口当作单调超时计时器。
uint64_t TimeBase_GetMs(void)
{
    return TimeBase_GetSnapshot().utc_ms;
}

// 取得 UTC、单调时间、误差与来源的一致时钟快照。复用公共 ABoxClock，并以平台单调毫秒时钟生成当前视图。
ABoxClockSnapshot TimeBase_GetSnapshot(void)
{ return ABoxClock_Snapshot(&g_clock, ABox_PortGetMonotonicMs()); }
// 使用同一可信时钟快照检查公共请求有效期。返回公共协议有效期响应码，不是布尔值；时钟未知和时间误差按平台规则处理。
uint16_t TimeBase_CheckExpiry(uint64_t expires)
{
    ABoxClockSnapshot snapshot = TimeBase_GetSnapshot();
    return ABoxMqttV4_CheckExpiry(&snapshot, expires);
}
// 用服务器时间和往返单调时间同步可信时钟。交给公共时钟校验误差及往返区间，成功后更新可追溯日志映射起点。
int TimeBase_Synchronize(uint64_t server, uint32_t error, uint64_t sent, uint64_t received)
{
    int ok = ABoxClock_Synchronize(&g_clock, server, error, sent, received);
    if (ok) g_log_mapping_start = ABox_PortGetMonotonicMs();
    return ok;
}

// 把有效 UTC 毫秒编码为公共协议整数文本。无可信时间或无输出容量返回失败并保留空串，不生成伪造的绝对时间。
uint8_t TimeBase_FormatUtcMs(char* out, uint16_t out_sz)
{
    if (!out || !out_sz) return 0U;
    out[0] = '\0';
    if (!TimeBase_IsValid()) return 0U;
    return (uint8_t)ABoxMqttV4_FormatInteger(out, out_sz, TimeBase_GetMs());
}

// 将当前可信 UTC 格式化为北京时间日期时间。先验证时间来源，再应用 UTC+8 偏移和公历换算，调用者提供足够文本容量。
uint8_t TimeBase_FormatBeijing(char* out, uint16_t out_sz)
{
    uint32_t sec;
    uint32_t days_total;
    uint32_t day_sec;
    int year = 1970;
    int mon = 1;
    int day;
    int hour;
    int min;
    int second;

    if ((out == 0) || (out_sz == 0U)) return 0U;
    out[0] = '\0';
    if (!TimeBase_IsValid()) return 0U;

    sec = (uint32_t)(TimeBase_GetMs() / 1000ULL);
    sec += TIMEBASE_BEIJING_OFFSET_SEC;
    days_total = sec / 86400U;
    day_sec = sec % 86400U;

    while (days_total >= (is_leap_year(year) ? 366U : 365U))
    {
        days_total -= is_leap_year(year) ? 366U : 365U;
        year++;
    }

    while (days_total >= month_days(year, mon))
    {
        days_total -= month_days(year, mon);
        mon++;
    }

    day = (int)days_total + 1;
    hour = (int)(day_sec / 3600U);
    min = (int)((day_sec % 3600U) / 60U);
    second = (int)(day_sec % 60U);

    snprintf(out, out_sz, "%04d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, min, second);
    return 1U;
}

// 把历史日志 tick 映射为对应北京时间。拒绝超过半周期、早于当前可信映射起点或不可追溯的时间，避免跨重启与校时错误映射。
uint8_t TimeBase_FormatBeijingFromTick(uint32_t tick, char* out, uint16_t out_sz)
{
    int64_t ms;
    ABoxClockSnapshot snapshot = TimeBase_GetSnapshot();
    uint32_t age = (uint32_t)snapshot.mono_ms - tick;
    uint32_t sec;
    uint32_t days_total;
    uint32_t day_sec;
    int year = 1970;
    int mon = 1;
    int day;
    int hour;
    int min;
    int second;

    if ((out == 0) || (out_sz == 0U)) return 0U;
    out[0] = '\0';
    if (!TimeBase_IsValid() || age > INT32_MAX ||
        snapshot.mono_ms < age || snapshot.mono_ms - age < g_log_mapping_start) return 0U;

    ms = (int64_t)ABoxClock_Snapshot(&g_clock, snapshot.mono_ms - age).utc_ms;
    if (ms < 0) return 0U;

    sec = (uint32_t)(ms / 1000LL);
    sec += TIMEBASE_BEIJING_OFFSET_SEC;
    days_total = sec / 86400U;
    day_sec = sec % 86400U;

    while (days_total >= (is_leap_year(year) ? 366U : 365U))
    {
        days_total -= is_leap_year(year) ? 366U : 365U;
        year++;
    }

    while (days_total >= month_days(year, mon))
    {
        days_total -= month_days(year, mon);
        mon++;
    }

    day = (int)days_total + 1;
    hour = (int)(day_sec / 3600U);
    min = (int)((day_sec % 3600U) / 60U);
    second = (int)(day_sec % 60U);

    snprintf(out, out_sz, "%04d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, min, second);
    return 1U;
}

// 查询当前是否已有可信时间来源。判断来源不是 NONE，不代表时间误差为零；严格有效期仍应使用快照与公共检查器。
uint8_t TimeBase_IsValid(void) { return g_clock.source != ABOX_CLOCK_NONE; }
