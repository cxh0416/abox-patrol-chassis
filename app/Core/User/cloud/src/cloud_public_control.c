#include "cloud_internal.h"
#include "chassis_cloud_protocol.h"
#include "patrol_power_ledger.h"
#include "remote_service.h"
#include "time_base.h"
#include <stdio.h>
#include <string.h>

// 将公共控制请求交给底盘远控服务并调度响应。公共协议与内部维护协议保持分离，响应缓冲区受限；实际会话和安全仲裁由 RemoteService 完成。
void CloudCommands_HandlePublicControl(const char* topic, const char* payload)
{
    /* Remote owns this workspace. ScheduleRawResponse copies it before a later
     * report reuses it; the transport also owns its own publish copy. */
    char *response = g_cloud.tx_payload;
    ABoxMqttRequest request;
    if (!topic || !payload || strlen(payload) > ABOX_MQTT_V4_PACKET_LIMIT) return;
    uint16_t decode_status = ChassisCloud_DecodeRequest(&request, payload, (uint16_t)strlen(payload));
    if (decode_status == 200U && strcmp(request.command, "sync_state") == 0) {
        const ABoxMqttOperation *operation;
        uint16_t code = ABoxMqttRequest_Resolve(&request, 0, 0, &operation);
        if (code == 200U) code = PatrolPowerLedger_CheckOther(topic, request.request_id);
        if (code != 200U) {
            char stamp[21];
            const char *message = code == 4001U ? "invalid sync_state request" :
                                  code == 4006U ? "requestId conflict" : "power ledger unavailable";
            if (!TimeBase_FormatUtcMs(stamp, sizeof(stamp))) strcpy(stamp, "0");
            (void)snprintf(response, sizeof(g_cloud.tx_payload),
                "{\"version\":\"4.0\",\"timestamp\":%s,\"data\":{\"requestId\":\"%s\",\"code\":%u,\"msg\":\"%s\"}}",
                stamp, request.request_id, (unsigned)code, message);
            (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp, response);
        } else {
            /* Preserve the correlated refresh until response and status slots are free. */
            (void)ABoxCloudService_QueueSyncRequest(&g_cloud.cloud, request.request_id);
        }
        ABoxMqttRequest_Close(&request);
        return;
    }
    ABoxMqttRequest_Close(&request);
    {
        int handled = PatrolPowerLedger_Intercept(topic, payload, response, sizeof(g_cloud.tx_payload));
        if (handled) {
            if (handled == 1) (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp, response);
            return;
        }
    }
    if (RemoteService_HandleControl(payload, (uint16_t)strlen(payload), response, sizeof(g_cloud.tx_payload)))
        (void)CloudMessages_ScheduleRawResponse(g_cloud.topic_control_resp, response);
    else APP_LOGW("REMOTE", "invalid or busy chassis control request");
}
