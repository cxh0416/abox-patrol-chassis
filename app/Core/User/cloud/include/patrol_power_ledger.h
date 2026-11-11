#ifndef PATROL_POWER_LEDGER_H
#define PATROL_POWER_LEDGER_H

#include <stdint.h>
#include "abox_ec800_at.h"

int PatrolPowerLedger_Init(ABoxEc800At *at);
void PatrolPowerLedger_AfterModemReset(void);
void PatrolPowerLedger_NetworkChanged(void);
void PatrolPowerLedger_Poll(void);
int PatrolPowerLedger_StorageBusy(void);
/* Check a different public command against protected power request IDs. */
uint16_t PatrolPowerLedger_CheckOther(const char *topic, const char *request_id);
/* 0: pass to normal handler; 1: response in buffer; 2: asynchronous. */
int PatrolPowerLedger_Intercept(const char *topic, const char *payload,
                                char *response, uint16_t capacity);

#endif
