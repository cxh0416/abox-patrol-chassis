#ifndef PATROL_FAULT_MAILBOX_H
#define PATROL_FAULT_MAILBOX_H

#include <stdint.h>

#define PATROL_FAULT_MAGIC 0x46544355U /* FTCU */
#define PATROL_FAULT_HARDFAULT 1U
#define PATROL_FAULT_STACK_OVERFLOW 2U
#define PATROL_FAULT_MALLOC_FAILED 3U

typedef struct {
    uint32_t magic;
    uint32_t reason;
    uint32_t pc;
    uint32_t lr;
    uint32_t crc32;
} PatrolFaultMailbox_t;

void PatrolFault_Record(uint32_t reason, uint32_t pc, uint32_t lr);
uint8_t PatrolFault_Valid(void);
void PatrolFault_Clear(void);

#endif
