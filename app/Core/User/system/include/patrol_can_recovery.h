#ifndef PATROL_CAN_RECOVERY_H
#define PATROL_CAN_RECOVERY_H

#include <stdint.h>

typedef struct {
    uint32_t first_ms;
    uint32_t last_sample_ms;
    uint8_t samples;
} PatrolCanRecovery;

// 清空 Bus-Off 恢复观察的起始时间和样本计数。新故障或供电丢失后重新观察，不能继承旧故障周期已经累计的稳定样本。
static inline void PatrolCanRecovery_Reset(PatrolCanRecovery* state)
{
    state->first_ms = 0U;
    state->last_sample_ms = 0U;
    state->samples = 0U;
}

/* A new, coherent feedback sample must follow the Bus-Off event. Stability
 * must persist for one second and include three distinct samples. */
// 统计故障之后健康且时间戳不同的恢复反馈。首次有效样本建立起点，至少三次不同样本且持续一秒才返回通过，重复或故障前样本不推进计数。
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
