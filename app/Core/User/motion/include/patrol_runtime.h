#ifndef PATROL_RUNTIME_H
#define PATROL_RUNTIME_H
#include <stdint.h>
#include "patrol_diagnostics.h"

uint8_t PatrolRuntime_BenchArm(uint16_t limit_mdeg);
uint8_t PatrolRuntime_BenchDriveArm(uint16_t limit_mdeg);
void PatrolRuntime_BenchStop(void);
void PatrolRuntime_FormatBench(char* out, uint16_t size);
void PatrolRuntime_GetDiagnostics(PatrolDiagnosticsSnapshot* snapshot);
void PatrolRuntime_Init(void);
void PatrolRuntime_Tick(void);
void PatrolRuntime_OnCanFrame(const PatrolCanFrame* frame, uint32_t now);
uint8_t PatrolRuntime_BeginEncoderCycle(PatrolDiagnosticsSnapshot* snapshot);
void PatrolRuntime_EncoderWriteCompleted(uint8_t state);
/* Encoder callbacks are invoked under the encoder task's critical section. */
void PatrolRuntime_OnEncoderConfig(const uint16_t* registers, uint32_t now);
void PatrolRuntime_OnEncoderCount(uint32_t count, uint32_t now);
void PatrolRuntime_OnEncoderError(PatrolEncoderError error);
#endif
