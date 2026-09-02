#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abox_ec800_at.h"
#include "abox_ec800_rx.h"
#include "cloud_protocol.h"
#include "mqtt_qmtrecv.h"

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(1); \
} } while (0)

static uint32_t g_tick;
static uint32_t g_message_count;
static CloudProtocolAction_t g_action;
static char g_request_id[CLOUD_PROTOCOL_REQUEST_ID_SIZE];

static uint32_t tick_ms(void *context) { (void)context; return g_tick; }
static int write_data(void *context, const uint8_t *data, uint16_t length,
                      uint32_t timeout_ms)
{
    (void)context;
    (void)data;
    (void)length;
    (void)timeout_ms;
    return 1;
}

static void mqtt_event(ABoxEc800Event type, const uint8_t *data,
                       uint16_t length, void *user)
{
    char line[ABOX_EC800_AT_LINE_SIZE];
    char topic[128];
    char payload[1280];
    MqttQmtRecvFrame_t frame = {0};
    CloudProtocolRequest_t request = {0};
    (void)user;
    if (type != ABOX_EC800_EVENT_LINE || length >= sizeof(line)) return;
    memcpy(line, data, length);
    line[length] = '\0';
    if (strstr(line, "+QMTRECV:") == 0) return;
    assert(MqttQmtRecv_DecodeLine(line, topic, sizeof(topic), payload,
                                  sizeof(payload), &frame));
    assert(frame.complete);
    assert(CloudProtocol_Parse(payload, (uint16_t)frame.payload_length, 0U,
                               &request) == CLOUD_PROTOCOL_OK);
    ++g_message_count;
    g_action = request.action;
    strncpy(g_request_id, request.request_id, sizeof(g_request_id) - 1U);
}

static void at_sink(void *context, const uint8_t *data, uint16_t length)
{
    ABoxEc800At_Feed((ABoxEc800At *)context, data, length);
}

static void dma_feed(ABoxEc800Rx *rx, ABoxEc800At *at, uint8_t *dma,
                     uint16_t *position, const uint8_t *data, uint16_t length)
{
    assert((uint32_t)*position + length < ABOX_EC800_RX_DMA_MIN);
    memcpy(dma + *position, data, length);
    *position = (uint16_t)(*position + length);
    assert(ABoxEc800Rx_OnDmaEvent(rx, *position));
    assert(ABoxEc800Rx_Drain(rx, at_sink, at, 0U) == length);
}

int main(void)
{
    ABoxEc800At at;
    ABoxEc800AtPort port = {0, tick_ms, write_data, 0, 0};
    ABoxEc800Rx rx;
    uint8_t dma[ABOX_EC800_RX_DMA_MIN] = {0};
    uint8_t ring[ABOX_EC800_RX_RING_MIN] = {0};
    uint16_t position = 0U;
    const char urc[] =
        "+QMTRECV: 0,0,\"/zxwl/abox/PATROL-CHASSIS-000/internal/config\",162,"
        "{\"version\":\"2.0\",\"timestamp\":1788245860188,\"data\":{"
        "\"requestId\":\"pipeline-info\",\"command\":\"internal_config\","
        "\"params\":{\"action\":\"get_info\"}}}\r\n";

    assert(ABoxEc800At_Init(&at, &port));
    assert(ABoxEc800At_Register(&at, ABOX_EC800_OWNER_MQTT, mqtt_event, 0));
    assert(ABoxEc800Rx_Init(&rx, dma, sizeof(dma), ring, sizeof(ring)));
    assert(ABoxEc800At_Submit(&at, "AT+QHTTPGET=80", ABOX_EC800_OWNER_OTA,
                              ABOX_EC800_PRIORITY_HIGH, 1000U, 0, 0));
    ABoxEc800At_Task(&at);

    dma_feed(&rx, &at, dma, &position, (const uint8_t *)urc, 7U);
    dma_feed(&rx, &at, dma, &position, (const uint8_t *)urc + 7U, 31U);
    dma_feed(&rx, &at, dma, &position, (const uint8_t *)urc + 38U,
             (uint16_t)(sizeof(urc) - 1U - 38U));

    assert(g_message_count == 1U);
    assert(g_action == CLOUD_PROTOCOL_ACTION_GET_INFO);
    assert(strcmp(g_request_id, "pipeline-info") == 0);
    assert(ABoxEc800Rx_OverflowCount(&rx) == 0U);
    assert(ABoxEc800At_LineOverflowCount(&at) == 0U);
    return 0;
}
