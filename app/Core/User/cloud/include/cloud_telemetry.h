#ifndef CLOUD_TELEMETRY_H
#define CLOUD_TELEMETRY_H

#include <stdint.h>

#define CLOUD_IDLE_STATUS_MS 120000U
#define CLOUD_IDLE_HEARTBEAT_MS 5000U
#define CLOUD_ACTIVE_STATUS_MS 1000U
#define CLOUD_ACTIVE_HEARTBEAT_MS 5000U
#define CLOUD_DIAGNOSTIC_LEASE_MS 30000U

typedef struct {
    uint32_t last_activity_ms;
    uint8_t activity_valid;
    uint8_t fast;
} CloudTelemetry;

// 登记最近一次需要快速诊断上报的活动时间。设置有效活动标记，由 Update 在租约到期后恢复普通上报节奏。
static inline void CloudTelemetry_Wake(CloudTelemetry* state, uint32_t now)
{
    state->last_activity_ms = now;
    state->activity_valid = 1U;
}

/* Unsigned elapsed time also expires leases across HAL tick wrap. */
// 更新快速诊断租约并检测进入快速模式的上升沿。活动刷新时间，过期清活动标记；仅从非快速转为快速时返回非零，而非简单返回当前模式。
static inline uint8_t CloudTelemetry_Update(CloudTelemetry* state, uint32_t now,
                                            uint8_t active)
{
    uint8_t previous = state->fast;
    if (active) CloudTelemetry_Wake(state, now);
    if (state->activity_valid && now - state->last_activity_ms >= CLOUD_DIAGNOSTIC_LEASE_MS)
        state->activity_valid = 0U;
    state->fast = state->activity_valid;
    return (uint8_t)(state->fast && !previous);
}

#endif
