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

// 把异常原因与 PC、LR 写入 Boot 共用故障邮箱。用于异常复位后的归因，不进行普通日志格式化或内部 Flash 擦写。
void PatrolFault_Record(uint32_t reason, uint32_t pc, uint32_t lr);
// 查询 Boot 共用故障邮箱记录是否有效。复用公共校验，不代表外部驱动器故障码有效。
uint8_t PatrolFault_Valid(void);
// 清除 Boot 共用故障邮箱标记。不清除运动故障日志、驱动器故障或 Config V5。
void PatrolFault_Clear(void);

#endif
