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

static inline void CloudTelemetry_Wake(CloudTelemetry* state, uint32_t now)
{
    state->last_activity_ms = now;
    state->activity_valid = 1U;
}

/* Unsigned elapsed time also expires leases across HAL tick wrap. */
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
