#ifndef BENCH_DRIVE_H
#define BENCH_DRIVE_H
#include "bench_steering.h"
/* Drive actuator limit shared by normal control. */
#define BENCH_DRIVE_MAX_RPM 500
typedef struct {
    BenchPhase phase;
    uint8_t step, pending, read, width, cut_power, confirmations, reversing;
    uint16_t index;
    uint32_t sent_ms, started_ms, zero_ms, left_ms, right_ms, fault;
    int32_t expected;
    int16_t desired, applied, writing;
    int8_t direction;
} BenchDrive;
void BenchDrive_Init(BenchDrive* b);
uint8_t BenchDrive_Start(BenchDrive* b, uint32_t now);
void BenchDrive_Stop(BenchDrive* b, uint32_t now, uint8_t emergency);
void BenchDrive_Tick(BenchDrive* b, uint32_t now, uint8_t requested, uint8_t safe,
    int16_t rpm, int32_t left, int32_t right, uint32_t left_ms, uint32_t right_ms);
uint8_t BenchDrive_Take(BenchDrive* b, uint32_t now, PatrolCanFrame* f);
uint8_t BenchDrive_OnFrame(BenchDrive* b, const PatrolCanFrame* f, uint32_t now);
void BenchDrive_TxFailed(BenchDrive* b);
#endif
