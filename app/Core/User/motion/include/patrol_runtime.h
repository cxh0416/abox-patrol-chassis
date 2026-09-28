#ifndef PATROL_RUNTIME_H
#define PATROL_RUNTIME_H
#include <stdint.h>
#include "motion_fault.h"
uint8_t PatrolRuntime_ResetFault(void);
void PatrolRuntime_BeginFaultEpisode(void);
void PatrolRuntime_CaptureFault(uint32_t source, uint32_t reason, uint32_t gates);
uint8_t PatrolRuntime_GetFault(MotionFaultRecord* record);
uint8_t PatrolRuntime_FaultStorage(void);
#include "patrol_diagnostics.h"

uint8_t PatrolRuntime_BenchArm(uint16_t limit_mdeg, const char* request_id);
uint8_t PatrolRuntime_BenchDriveArm(uint16_t limit_mdeg, const char* request_id);
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
