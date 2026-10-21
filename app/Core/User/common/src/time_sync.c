#include "time_sync.h"
#include "time_base.h"
#include "abox_time_sync.h"
#include "abox_platform_port.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "app_log.h"
#include "main.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/* VID <= 31: request/response topics need at most 65/66 bytes.
 * Clock state is initialized explicitly; BSS stays below the Boot fault mailbox. */
static ABoxTimeSync g_sync;
static char g_request_topic[72];
static char g_response_topic[72];
static uint64_t g_boot_seed;
static uint32_t g_ready_sequence;
// 构造并提交一次公共时间同步请求。已有发布时暂缓，当前 UTC 无效时按协议发送零时间戳；返回提交结果不表示已取得服务器响应。
static int send(void *context, const char *id)
{
    char packet[192];
    char utc[21];
    (void)context;
    if (MQTT_Core_IsPublishActive()) return 0;
    if (!TimeBase_FormatUtcMs(utc, sizeof(utc))) strcpy(utc, "0");
    snprintf(packet,sizeof(packet),"{\"version\":\"2.0\",\"timestamp\":%s,\"data\":{"
             "\"requestId\":\"%s\",\"command\":\"sync_time\",\"params\":{}}}",utc,id);
    return MQTT_Core_PublishEx(g_request_topic,packet,1U,0U);
}
// 将时间同步得到的服务器时间与误差交给可信时钟。使用请求发送及响应接收的单调时间校验往返区间，返回实际同步结果。
static int apply(void *context, uint64_t utc, uint32_t error, uint64_t sent, uint64_t received)
{
    int ok;
    (void)context;
    ok = TimeBase_Synchronize(utc,error,sent,received);
    if (ok) {
        ABoxClockSnapshot snapshot = TimeBase_GetSnapshot();
        APP_LOGI("TIME","platform rtt=%lu err=%lu",(unsigned long)(received-sent),
                 (unsigned long)snapshot.error_ms);
    }
    return ok;
}
// 按设备身份初始化公共授时状态与请求响应 Topic。建立本轮请求种子和连接序号上下文，避免旧连接响应匹配新的授时请求。
void TimeSync_Init(const char *device_id)
{
    uint32_t sequence = g_boot_seed ? g_sync.sequence : 0U;
    ABoxTimeSync_Init(&g_sync);
    g_sync.sequence = sequence; /* Reconfiguration cannot reuse an in-boot ID. */
    snprintf(g_request_topic,sizeof(g_request_topic),"/zxwl/abox/%s/internal/time/request",device_id);
    snprintf(g_response_topic,sizeof(g_response_topic),"/zxwl/abox/%s/internal/time/response",device_id);
}
// 返回本设备时间同步响应 Topic。文本存储由模块持有，初始化或身份变化后内容可能更新，调用者不得修改或释放。
const char *TimeSync_ResponseTopic(void) { return g_response_topic; }
// 在连接与发送条件允许时推进公共授时轮询。检测新的就绪周期并通过公共服务管理请求和超时，不阻塞等待网络时间。
void TimeSync_Task(void)
{
    static const ABoxTimeSyncPort port = {0,send,apply};
    uint64_t mono = ABox_PortGetMonotonicMs();
    uint8_t ready = MQTT_Core_IsReady() && MqttRuntime_ActiveTls();
    if (ready && g_ready_sequence != MQTT_Core_GetReadySeq()) {
        g_ready_sequence = MQTT_Core_GetReadySeq();
        g_sync.ready = g_sync.pending = g_sync.attempts = g_sync.have_best = 0U;
    }
    /* Platform UTC must remain available even if CCLK bootstrap was rejected. */
    if (!g_boot_seed && ready) {
        g_boot_seed = TimeBase_GetMs() ^ mono ^ ((uint64_t)HAL_GetUIDw0() << 32) ^ HAL_GetUIDw1();
        if (!g_boot_seed) g_boot_seed = 1U;
    }
    ABoxTimeSync_Poll(&g_sync,&port,ready && g_boot_seed != 0,mono,g_boot_seed);
}
// 将合法非负 JSON 整数提取为 64 位值。核对数字类型、有限性和范围，避免从小数或负数直接转换成时间戳。
static int integer(const cJSON *value, uint64_t *out)
{
    if (!cJSON_IsNumber(value) || value->valuedouble < 0 ||
        value->valuedouble > 9007199254740991.0 ||
        value->valuedouble != (double)(uint64_t)value->valuedouble) return 0;
    *out = (uint64_t)value->valuedouble; return 1;
}
// 识别并校验本设备授时响应。验证 Topic、协议版本、requestId、状态码、服务器时间及误差，再交给公共同步服务关联本轮请求。
int TimeSync_OnMessage(const char *topic, const char *payload)
{
    cJSON *root, *data, *id, *version, *code, *result;
    uint64_t utc, error;
    uint64_t received = ABox_PortGetMonotonicMs();
    if (strcmp(topic,g_response_topic)) return 0;
    if (!MqttRuntime_ActiveTls()) return 1;
    root = cJSON_Parse(payload);
    if (!root) return 1;
    version = cJSON_GetObjectItemCaseSensitive(root,"version");
    data = cJSON_GetObjectItemCaseSensitive(root,"data");
    id = cJSON_GetObjectItemCaseSensitive(data,"requestId");
    code = cJSON_GetObjectItemCaseSensitive(data,"code");
    result = cJSON_GetObjectItemCaseSensitive(data,"result");
    if (cJSON_IsString(version) && !strcmp(version->valuestring,"2.0") &&
        cJSON_IsString(id) && cJSON_IsNumber(code) && code->valuedouble == 200 &&
        integer(cJSON_GetObjectItemCaseSensitive(result,"serverUtcMs"),&utc) &&
        integer(cJSON_GetObjectItemCaseSensitive(result,"errorMs"),&error) && error <= ABOX_CLOCK_MAX_REFERENCE_ERROR_MS)
        (void)ABoxTimeSync_Response(&g_sync,id->valuestring,utc,(uint32_t)error,received);
    cJSON_Delete(root);
    return 1;
}
