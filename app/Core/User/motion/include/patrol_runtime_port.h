#ifndef PATROL_RUNTIME_PORT_H
#define PATROL_RUNTIME_PORT_H
#include <stdint.h>
#include "patrol_canopen.h"

/* Implemented by the RTOS adapter or deterministic host stubs. Locks nest. */
uint32_t PatrolRuntimePort_Now(void);
void PatrolRuntimePort_Enter(void);
void PatrolRuntimePort_Exit(void);
uint8_t PatrolRuntimePort_CanBusOff(void);
uint8_t PatrolRuntimePort_CanBusOffActive(void);
uint8_t PatrolRuntimePort_OtaBusy(void);
uint8_t PatrolRuntimePort_CanTxReady(void);
void PatrolRuntimePort_AbortCanTx(void);
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* frame);
#endif
