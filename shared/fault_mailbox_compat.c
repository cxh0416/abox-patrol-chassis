#include "fault_mailbox.h"
#include "abox_boot_v2.h"

void PatrolFault_Record(uint32_t reason, uint32_t pc, uint32_t lr)
{
    ABoxBootV2_FaultRecord(reason, pc, lr);
}

uint8_t PatrolFault_Valid(void) { return (uint8_t)ABoxBootV2_FaultValid(); }
void PatrolFault_Clear(void) { ABoxBootV2_FaultClear(); }
