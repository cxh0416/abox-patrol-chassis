#include "fault_mailbox.h"
#include "abox_boot_v2.h"

// 把异常原因与 PC、LR 写入 Boot 共用故障邮箱。用于异常复位后的归因，不进行普通日志格式化或内部 Flash 擦写。
void PatrolFault_Record(uint32_t reason, uint32_t pc, uint32_t lr)
{
    ABoxBootV2_FaultRecord(reason, pc, lr);
}

// 查询 Boot 共用故障邮箱记录是否有效。复用公共校验，不代表外部驱动器故障码有效。
uint8_t PatrolFault_Valid(void) { return (uint8_t)ABoxBootV2_FaultValid(); }
// 清除 Boot 共用故障邮箱标记。不清除运动故障日志、驱动器故障或 Config V5。
void PatrolFault_Clear(void) { ABoxBootV2_FaultClear(); }
