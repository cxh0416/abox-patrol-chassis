#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "abox_log_ring.h"
#include "patrol_diag_config.h"
#include "main.h"
#include "time_base.h"
#if PATROL_EC800_AT_DIAG_UART5 || defined(PATROL_OTA_FIELD_PROGRESS_UART5)
#include "usart.h"
#endif

static ABoxLogRing g_log;

// 把日志原始 tick 映射为可用的北京时间文本。复用可信时间映射，时间不可追溯时返回失败，不能把启动后毫秒直接解释为日历时间。
static int format_time(uint32_t tick, char *output, uint16_t capacity)
{
    return TimeBase_FormatBeijingFromTick(tick, output, capacity);
}

// 初始化产品日志环形缓冲区。复用公共 ABoxLogRing，不创建阻塞式串口发送事务。
void AppLog_Init(void)
{
    ABoxLogRing_Init(&g_log);
}

// 清空当前 RAM 日志环。仅影响运行日志，不清除 Flash 故障记录或仓库中的不可变现场证据。
void AppLog_Clear(void)
{
    ABoxLogRing_Clear(&g_log);
}

// 设置日志环允许记录的等级。实际等级语义复用公共日志服务，后续 Printf 先按该等级过滤。
void AppLog_SetLevel(AppLogLevel_t level)
{
    ABoxLogRing_SetLevel(&g_log, (uint8_t)level);
}

// 读取当前日志记录等级。返回等级枚举而非布尔结果，不改变已存日志。
AppLogLevel_t AppLog_GetLevel(void)
{
    return (AppLogLevel_t)ABoxLogRing_GetLevel(&g_log);
}

// 在专用现场观察构建中输出启动标记。受 UART5 观察宏控制，普通构建不发送；用于采集证据，不能替代 Boot 或运动验收。
void AppLog_FieldStartup(const char *version)
{
#ifdef PATROL_OTA_FIELD_PROGRESS_UART5
    char line[128];
    int n = snprintf(line, sizeof(line),
                     "[APP2] evt=APP_START version=%s fieldBuild=1 role=OTA_OBSERVE\r\n",
                     version == 0 ? "unknown" : version);
    if (n > 0) (void)HAL_UART_Transmit(&huart5, (uint8_t *)line, (uint16_t)n, 100U);
#else
    (void)version;
#endif
}

// 在专用现场构建中输出 OTA 下载阶段进度。记录阈值、实际百分比、字节数和槽号，普通构建不发送，进度不是安装或启动完成证明。
void AppLog_FieldOtaProgress(uint32_t threshold_percent, uint32_t actual_percent,
                             uint32_t downloaded, uint32_t size, uint8_t slot)
{
#ifdef PATROL_OTA_FIELD_PROGRESS_UART5
    char line[160];
    int n = snprintf(line, sizeof(line),
                     "[APP2] evt=OTA_DOWNLOAD_PROGRESS threshold=%lu actualPercent=%lu downloaded=%lu size=%lu slot=%u\r\n",
                     (unsigned long)threshold_percent, (unsigned long)actual_percent,
                     (unsigned long)downloaded, (unsigned long)size, (unsigned)slot);
    if (n > 0) (void)HAL_UART_Transmit(&huart5, (uint8_t *)line, (uint16_t)n, 100U);
#else
    (void)threshold_percent;
    (void)actual_percent;
    (void)downloaded;
    (void)size;
    (void)slot;
#endif
}

// 格式化一条受等级过滤的日志并压入 RAM 环。使用受限局部缓冲，短临界区保护入环且恢复原中断状态；不在本接口等待 MQTT 发布。
void AppLog_Printf(AppLogLevel_t level, const char *tag, const char *format, ...)
{
    char message[64];
    va_list arguments;
    uint32_t primask;
    if (!format || level > AppLog_GetLevel()) return;
    va_start(arguments, format);
    if (vsnprintf(message, sizeof(message), format, arguments) < 0) {
        va_end(arguments);
        return;
    }
    va_end(arguments);
    primask = __get_PRIMASK();
    __disable_irq();
    ABoxLogRing_PushText(&g_log, HAL_GetTick(), (uint8_t)level, tag, message);
    if (!primask) __enable_irq();
}

// 将当前日志环导出到有界文本缓冲。可用时映射日志时间，返回输出长度，实际容量及截断遵循公共环形服务约定。
uint16_t AppLog_Dump(char *output, uint16_t capacity)
{
    return ABoxLogRing_Dump(&g_log, output, capacity, format_time);
}

// 从日志环的 MQTT 游标读取一条普通日志。返回本次输出长度，空或容量不满足时按公共接口结果处理，不代表消息已经发布。
uint16_t AppLog_ReadMqttLine(char *output, uint16_t capacity)
{
    return ABoxLogRing_ReadMqttLine(&g_log, output, capacity, format_time);
}

// 在短临界区领取下一条 MQTT 告警及等级。仅在确实读到文本时写回 level，出临界区后才由调用方编码和发送。
uint16_t AppLog_ReadMqttAlert(char *output, uint16_t capacity,
                              AppLogLevel_t *level)
{
    uint8_t value = 0U;
    uint16_t length;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    length = ABoxLogRing_ReadMqttAlert(&g_log, output, capacity,
                                       level ? &value : 0, format_time);
    if (!primask) __enable_irq();
    if (length && level) *level = (AppLogLevel_t)value;
    return length;
}

// 读取 MQTT 日志消费路径的丢弃累计计数。供诊断采集完整性使用，不等同于 CAN 丢帧或运动故障计数。
uint32_t AppLog_GetMqttDroppedCount(void)
{
    return ABoxLogRing_MqttDropped(&g_log);
}

#if PATROL_EC800_AT_DIAG_UART5
// 按编译开关向 UART5 输出下一条调试日志。启用分支先在短临界区取一行再阻塞发送，禁用分支为空实现，不占用运动调度的关键临界区。
void AppLog_DebugTask(void)
{
    char line[128];
    uint16_t length;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    length = ABoxLogRing_ReadDebugLine(&g_log, line, sizeof(line), format_time);
    if (!primask) __enable_irq();
    if (length) (void)HAL_UART_Transmit(&huart5, (uint8_t *)line, length, 1000U);
}
#else
// 按编译开关向 UART5 输出下一条调试日志。启用分支先在短临界区取一行再阻塞发送，禁用分支为空实现，不占用运动调度的关键临界区。
void AppLog_DebugTask(void) {}
#endif
