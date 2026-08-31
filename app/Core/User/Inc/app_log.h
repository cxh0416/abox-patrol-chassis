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

void AppLog_Init(void);
void AppLog_Clear(void);
void AppLog_Printf(AppLogLevel_t level, const char* tag, const char* fmt, ...);
uint16_t AppLog_Dump(char* out, uint16_t out_sz);
uint16_t AppLog_ReadMqttLine(char* out, uint16_t out_sz);
uint16_t AppLog_ReadMqttAlert(char* out, uint16_t out_sz, AppLogLevel_t* level);
uint32_t AppLog_GetMqttDroppedCount(void);
void AppLog_DebugTask(void);
void AppLog_SetLevel(AppLogLevel_t level);
AppLogLevel_t AppLog_GetLevel(void);
void AppLog_FieldStartup(const char* version);
void AppLog_FieldOtaProgress(uint32_t threshold_percent, uint32_t actual_percent,
                             uint32_t downloaded, uint32_t size, uint8_t slot);

#define APP_LOGE(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define APP_LOGW(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define APP_LOGI(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define APP_LOGD(tag, fmt, ...) AppLog_Printf(APP_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)

#endif
