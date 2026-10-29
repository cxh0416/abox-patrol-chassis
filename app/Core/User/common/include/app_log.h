#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>

typedef enum
{
    APP_LOG_LEVEL_ERROR = 0,
    APP_LOG_LEVEL_WARN = 1,
    APP_LOG_LEVEL_INFO = 2,
    APP_LOG_LEVEL_DEBUG = 3
} AppLogLevel_t;

// 初始化产品日志环形缓冲区。复用公共 ABoxLogRing，不创建阻塞式串口发送事务。
void AppLog_Init(void);
// 清空当前 RAM 日志环。仅影响运行日志，不清除 Flash 故障记录或仓库中的不可变现场证据。
void AppLog_Clear(void);
// 格式化一条受等级过滤的日志并压入 RAM 环。使用受限局部缓冲，短临界区保护入环且恢复原中断状态；不在本接口等待 MQTT 发布。
void AppLog_Printf(AppLogLevel_t level, const char* tag, const char* fmt, ...);
// 将当前日志环导出到有界文本缓冲。可用时映射日志时间，返回输出长度，实际容量及截断遵循公共环形服务约定。
uint16_t AppLog_Dump(char* out, uint16_t out_sz);
// 从日志环的 MQTT 游标读取一条普通日志。返回本次输出长度，空或容量不满足时按公共接口结果处理，不代表消息已经发布。
uint16_t AppLog_ReadMqttLine(char* out, uint16_t out_sz);
// 在短临界区领取下一条 MQTT 告警及等级。仅在确实读到文本时写回 level，出临界区后才由调用方编码和发送。
uint16_t AppLog_ReadMqttAlert(char* out, uint16_t out_sz, AppLogLevel_t* level);
// 读取 MQTT 日志消费路径的丢弃累计计数。供诊断采集完整性使用，不等同于 CAN 丢帧或运动故障计数。
uint32_t AppLog_GetMqttDroppedCount(void);
// 按编译开关向 UART5 输出下一条调试日志。启用分支先在短临界区取一行再阻塞发送，禁用分支为空实现，不占用运动调度的关键临界区。
void AppLog_DebugTask(void);
// 设置日志环允许记录的等级。实际等级语义复用公共日志服务，后续 Printf 先按该等级过滤。
void AppLog_SetLevel(AppLogLevel_t level);
// 读取当前日志记录等级。返回等级枚举而非布尔结果，不改变已存日志。
AppLogLevel_t AppLog_GetLevel(void);
// 在专用现场观察构建中输出启动标记。受 UART5 观察宏控制，普通构建不发送；用于采集证据，不能替代 Boot 或运动验收。
void AppLog_FieldStartup(const char* version);
// 在专用现场构建中输出 OTA 下载阶段进度。记录阈值、实际百分比、字节数和槽号，普通构建不发送，进度不是安装或启动完成证明。
void AppLog_FieldOtaProgress(uint32_t threshold_percent, uint32_t actual_percent,
                             uint32_t downloaded, uint32_t size, uint8_t slot);

#define APP_LOGE(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define APP_LOGW(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define APP_LOGI(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define APP_LOGD(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)

#endif
