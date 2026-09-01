#include <stdio.h>
#include <string.h>

#include "cloud_protocol.h"
#include "mqtt_qmtrecv.h"

static int expect_request(const char *name, const char *line, uint8_t ota,
                          CloudProtocolAction_t action, const char *request_id)
{
    char topic[128];
    char payload[1152];
    MqttQmtRecvFrame_t frame = {0};
    CloudProtocolRequest_t request = {0};

    if (!MqttQmtRecv_DecodeLine(line, topic, sizeof(topic), payload, sizeof(payload), &frame) ||
        !frame.complete || CloudProtocol_Parse(payload, (uint16_t)frame.payload_length, ota, &request) != CLOUD_PROTOCOL_OK ||
        request.action != action || strcmp(request.request_id, request_id) != 0)
    {
        fprintf(stderr, "%s failed: complete=%u escaped=%u payload=%s request=%s\n",
                name, frame.complete, frame.outer_escaped, payload, request.request_id);
        return 0;
    }
    return 1;
}

int main(void)
{
    const char *get_info =
        "+QMTRECV: 0,0,\"/zxwl/abox/PATROL-CHASSIS-000/internal/config\","
        "{\"version\":\"2.0\",\"timestamp\":1788245860188,\"data\":{\"requestId\":\"req-info\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"get_info\"}}}";
    const char *get_info_with_length_and_outer_escape =
        "+QMTRECV: 0,0,\"/zxwl/abox/PATROL-CHASSIS-000/internal/config\",162,"
        "\"{\\\"version\\\":\\\"2.0\\\",\\\"timestamp\\\":1788245860188,\\\"data\\\":{"
        "\\\"requestId\\\":\\\"req-escaped\\\",\\\"command\\\":\\\"internal_config\\\","
        "\\\"params\\\":{\\\"action\\\":\\\"get_info\\\"}}}\"";
    const char *get_info_with_length =
        "+QMTRECV: 0,0,\"/zxwl/abox/PATROL-CHASSIS-000/internal/config\",162,"
        "{\"version\":\"2.0\",\"timestamp\":1788245860188,\"data\":{\"requestId\":\"req-length\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"get_info\"}}}";
    const char *ota_with_length_and_outer_escape =
        "+QMTRECV: 0,0,\"/zxwl/abox/PATROL-CHASSIS-000/internal/ota\",332,"
        "\"{\\\"version\\\":\\\"2.0\\\",\\\"timestamp\\\":1788245340369,\\\"data\\\":{"
        "\\\"requestId\\\":\\\"req-ota\\\",\\\"command\\\":\\\"ota_upgrade\\\",\\\"params\\\":{"
        "\\\"url\\\":\\\"https://ota.zxwl.ntiov.com:20443/fw-revisions/patrol_vehicle_chassis/"
        "patrol-chassis-0.1.3/patrol_vehicle_chassis_app.bin\\\",\\\"version\\\":\\\"patrol-chassis-0.1.3\\\","
        "\\\"size\\\":85000,\\\"crc32\\\":\\\"1234ABCD\\\"}}}\"";
    char topic[32];
    char payload[32];
    MqttQmtRecvFrame_t frame;

    if (!expect_request("get_info", get_info, 0U, CLOUD_PROTOCOL_ACTION_GET_INFO, "req-info")) return 1;
    if (!expect_request("get_info escaped", get_info_with_length_and_outer_escape, 0U,
                        CLOUD_PROTOCOL_ACTION_GET_INFO, "req-escaped")) return 1;
    if (!expect_request("get_info length", get_info_with_length, 0U,
                        CLOUD_PROTOCOL_ACTION_GET_INFO, "req-length")) return 1;
    if (!expect_request("ota escaped", ota_with_length_and_outer_escape, 1U,
                        CLOUD_PROTOCOL_ACTION_OTA, "req-ota")) return 1;
    if (MqttQmtRecv_DecodeLine("+QMTRECV: 0,0,\"topic\",12,\"not-json\"",
                               topic, sizeof(topic), payload, sizeof(payload), &frame)) return 1;
    return 0;
}
