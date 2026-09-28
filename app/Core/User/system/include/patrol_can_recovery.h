#ifndef PATROL_CAN_RECOVERY_H
#define PATROL_CAN_RECOVERY_H

#include <stdint.h>

typedef struct {
    uint32_t first_ms;
    uint32_t last_sample_ms;
    uint8_t samples;
} PatrolCanRecovery;

static inline void PatrolCanRecovery_Reset(PatrolCanRecovery* state)
{
    state->first_ms = 0U;
    state->last_sample_ms = 0U;
    state->samples = 0U;
}

/* A new, coherent feedback sample must follow the Bus-Off event. Stability
 * must persist for one second and include three distinct samples. */
static inline uint8_t PatrolCanRecovery_Observe(PatrolCanRecovery* state,
    uint32_t now, uint32_t sample_ms, uint32_t event_ms, uint8_t healthy)
{
    /* A polling gap may briefly make feedback stale. Only count good,
     * distinct samples; a new Bus-Off or power loss resets the sequence. */
    if (!healthy || sample_ms == 0U || (int32_t)(sample_ms - event_ms) <= 0)
        return 0U;
    if (state->samples == 0U) state->first_ms = now;
    if (sample_ms != state->last_sample_ms && state->samples < 3U) ++state->samples;
    state->last_sample_ms = sample_ms;
    return state->samples >= 3U && (uint32_t)(now - state->first_ms) >= 1000U;
}

#endif
