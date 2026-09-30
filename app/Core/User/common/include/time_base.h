#ifndef __TIME_BASE_H__
#define __TIME_BASE_H__

#include <stdint.h>
#include "abox_clock.h"

void TimeBase_Init(void);

/* 喂入 AT+CCLK? 返回的字符串 */
void TimeBase_UpdateFromCCLK(const char* line);
ABoxClockSnapshot TimeBase_GetSnapshot(void);
uint16_t TimeBase_CheckExpiry(uint64_t expires_at);
int TimeBase_Synchronize(uint64_t server_ms, uint32_t error_ms,
                           uint64_t sent_mono, uint64_t received_mono);

/* 获取毫秒时间戳 */
uint64_t TimeBase_GetMs(void);
uint8_t TimeBase_FormatUtcMs(char* out, uint16_t out_sz);
uint8_t TimeBase_FormatBeijing(char* out, uint16_t out_sz);
uint8_t TimeBase_FormatBeijingFromTick(uint32_t tick, char* out, uint16_t out_sz);

/* UTC 已初始化；不代表可信。执行有效期检查应使用 ClockSnapshot。 */
uint8_t TimeBase_IsValid(void);

#endif
