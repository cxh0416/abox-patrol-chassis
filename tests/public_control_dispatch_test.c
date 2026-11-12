#include <assert.h>
#include <string.h>

/* Exercise the real control dispatcher while stubbing only its downstream ports. */
#include "cloud_internal.h"
#include "chassis_cloud_protocol.h"
#include "cJSON.h"

CloudContext g_cloud;
static unsigned sync_count, intercept_count, remote_count, response_count;
static uint16_t ledger_code = 200U;
static char response_copy[1536];

uint16_t PatrolPowerLedger_CheckOther(const char *topic, const char *id)
{ (void)topic; (void)id; return ledger_code; }
int PatrolPowerLedger_Intercept(const char *topic, const char *payload, char *out, uint16_t cap)
{ (void)topic; (void)payload; (void)out; (void)cap; ++intercept_count; return 0; }
uint8_t RemoteService_HandleControl(const char *payload, uint16_t length, char *out, uint16_t cap)
{ (void)payload; (void)length; (void)out; (void)cap; ++remote_count; return 0U; }
int ABoxCloudService_QueueSyncRequest(ABoxCloudService *service, const char *id)
{ (void)service; assert(!strcmp(id, "good-1")); ++sync_count; return 1; }
uint8_t TimeBase_FormatUtcMs(char *out, uint16_t size)
{ if (size < 2U) return 0U; strcpy(out, "1"); return 1U; }
uint8_t CloudMessages_ScheduleRawResponse(const char *topic, const char *payload)
{ (void)topic; ++response_count; strcpy(response_copy, payload); return 1U; }
void AppLog_Printf(AppLogLevel_t level, const char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; }

int main(void)
{
    const char *good = "{\"version\":\"4.0\",\"timestamp\":1,\"data\":{\"requestId\":\"good-1\",\"command\":\"sync_state\",\"params\":null}}";
    const char *bad[] = {
        "{\"version\":\"4.0\",\"timestamp\":1,\"data\":{\"requestId\":\"bad\\\"id\",\"command\":\"sync_state\",\"params\":null}}",
        "{\"version\":\"4.0\",\"timestamp\":1e0,\"data\":{\"requestId\":\"good-1\",\"command\":\"sync_state\",\"params\":null}}",
        "{\"version\":\"4.0\",\"timestamp\":1,\"data\":{\"requestId\":\"good-1\",\"command\":\"sync_state\",\"command\":\"power_off\",\"params\":null}}",
        "{\"version\":\"4.0\",\"timestamp\":1,\"data\":{\"requestId\":\"good-1\",\"command\":\"sync_state\",\"params\":null}}junk"
    };
    CloudCommands_HandlePublicControl("topic", good);
    assert(sync_count == 1U && intercept_count == 0U && remote_count == 0U);
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) CloudCommands_HandlePublicControl("topic", bad[i]);
    assert(sync_count == 1U && intercept_count == 4U && remote_count == 4U);
    ledger_code = 4006U;
    CloudCommands_HandlePublicControl("topic", good);
    assert(sync_count == 1U && response_count == 1U);
    cJSON *parsed = cJSON_Parse(response_copy);
    assert(parsed && !strcmp(cJSON_GetObjectItemCaseSensitive(parsed, "version")->valuestring, "4.0"));
    assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(parsed, "data"), "code")->valueint == 4006);
    cJSON_Delete(parsed);
    const char *expiry = "{\"version\":\"4.0\",\"timestamp\":1,\"data\":{\"requestId\":\"good-1\",\"command\":\"sync_state\",\"params\":null,\"expiresAt\":2}}";
    CloudCommands_HandlePublicControl("topic", expiry);
    assert(sync_count == 1U && response_count == 2U);
    parsed = cJSON_Parse(response_copy);
    assert(parsed && cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(parsed, "data"), "code")->valueint == 4001);
    cJSON_Delete(parsed);
    char oversized[1026];
    memset(oversized, ' ', sizeof(oversized) - 1U); oversized[sizeof(oversized) - 1U] = '\0';
    CloudCommands_HandlePublicControl("topic", oversized);
    assert(sync_count == 1U && intercept_count == 4U && remote_count == 4U);
    return 0;
}
