#ifndef __TIME_BASE_H__
#define __TIME_BASE_H__

#include <stdint.h>
#include "abox_clock.h"

// 初始化可信时钟与模块时区模式观察状态。初始无 UTC 来源，日志 tick 到日历时间的可追溯起点在成功引导或同步后建立。
void TimeBase_Init(void);

/* 喂入 AT+CCLK? 返回的字符串 */
// 观察模块时区模式和时钟 AT 行以尝试引导 UTC。RDY 清除旧模式，未知 CTZU 模式拒绝 CCLK 引导，避免把本地时钟误当 UTC。
void TimeBase_UpdateFromCCLK(const char* line);
// 取得 UTC、单调时间、误差与来源的一致时钟快照。复用公共 ABoxClock，并以平台单调毫秒时钟生成当前视图。
ABoxClockSnapshot TimeBase_GetSnapshot(void);
// 使用同一可信时钟快照检查公共请求有效期。返回公共协议有效期响应码，不是布尔值；时钟未知和时间误差按平台规则处理。
uint16_t TimeBase_CheckExpiry(uint64_t expires_at);
// 用服务器时间和往返单调时间同步可信时钟。交给公共时钟校验误差及往返区间，成功后更新可追溯日志映射起点。
int TimeBase_Synchronize(uint64_t server_ms, uint32_t error_ms,
                           uint64_t sent_mono, uint64_t received_mono);

/* 获取毫秒时间戳 */
// 读取当前快照中的 UTC 毫秒值。调用者需要可信绝对时间时应同时检查有效来源，不能把该接口当作单调超时计时器。
uint64_t TimeBase_GetMs(void);
// 把有效 UTC 毫秒编码为公共协议整数文本。无可信时间或无输出容量返回失败并保留空串，不生成伪造的绝对时间。
uint8_t TimeBase_FormatUtcMs(char* out, uint16_t out_sz);
// 将当前可信 UTC 格式化为北京时间日期时间。先验证时间来源，再应用 UTC+8 偏移和公历换算，调用者提供足够文本容量。
uint8_t TimeBase_FormatBeijing(char* out, uint16_t out_sz);
// 把历史日志 tick 映射为对应北京时间。拒绝超过半周期、早于当前可信映射起点或不可追溯的时间，避免跨重启与校时错误映射。
uint8_t TimeBase_FormatBeijingFromTick(uint32_t tick, char* out, uint16_t out_sz);

/* UTC 已初始化；不代表可信。执行有效期检查应使用 ClockSnapshot。 */
// 查询当前是否已有可信时间来源。判断来源不是 NONE，不代表时间误差为零；严格有效期仍应使用快照与公共检查器。
uint8_t TimeBase_IsValid(void);

#endif
