#ifndef BENCH_STEERING_H
#define BENCH_STEERING_H
#include <stdint.h>
#include "patrol_canopen.h"

typedef enum { BENCH_IDLE, BENCH_PREPARING, BENCH_ACTIVE, BENCH_STOPPING, BENCH_FAULT } BenchPhase;
typedef struct {
    BenchPhase phase;
    uint8_t step, pending, read, width, cut_power;
    uint16_t index;
    uint32_t sent_ms, started_ms, stop_ms, fault;
    int32_t minimum, maximum, center, hold, desired, applied, expected;
    uint16_t limit_mdeg;
} BenchSteering;

void BenchSteering_Init(BenchSteering* b);
uint8_t BenchSteering_Start(BenchSteering* b, int32_t actual, int32_t center, uint16_t limit, uint32_t now);
void BenchSteering_Stop(BenchSteering* b, uint32_t now, uint8_t cut_power);
void BenchSteering_Tick(BenchSteering* b, uint32_t now, uint8_t requested, uint8_t safe, int32_t angle);
uint8_t BenchSteering_Take(BenchSteering* b, uint32_t now, PatrolCanFrame* frame);
uint8_t BenchSteering_OnFrame(BenchSteering* b, const PatrolCanFrame* frame, uint32_t now);
void BenchSteering_TxFailed(BenchSteering* b);
#endif
