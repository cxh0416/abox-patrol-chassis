#include "mqtt_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(check) do { if (!(check)) { \
    fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #check); \
    exit(1); \
} } while (0)

static ABoxEc800At at;
static uint32_t now_ms;
static char command[256];
static unsigned messages, revoked;
static char last_topic[128], last_payload[64];

uint32_t HAL_GetTick(void) { return now_ms; }
static uint32_t tick(void *context) { (void)context; return now_ms; }
static int write_bytes(void *context, const uint8_t *bytes, uint16_t length,
                       uint32_t timeout)
{
    (void)context; (void)timeout;
    if (length >= 2U && bytes[0] == 'A' && bytes[1] == 'T') {
        assert(length < sizeof(command));
        memcpy(command, bytes, length);
        command[length] = 0;
    }
    return 1;
}
static void message(const char *topic, const char *payload)
{
    ++messages;
    snprintf(last_topic, sizeof(last_topic), "%s", topic);
    snprintf(last_payload, sizeof(last_payload), "%s", payload);
}
static void network_changed(void) { ++revoked; }
static void feed(const char *bytes)
{
    ABoxEc800At_Feed(&at, (const uint8_t *)bytes, (uint16_t)strlen(bytes));
}
static void cycle(void)
{
    MQTT_Core_Task();
    ABoxEc800At_Task(&at);
}
static void answer(const char *expected, const char *reply)
{
    unsigned i;
    for (i = 0U; i < 4U && !command[0]; ++i) cycle();
    assert(!strncmp(command, expected, strlen(expected)));
    command[0] = 0;
    feed(reply);
    ++now_ms;
    cycle();
}
int main(void)
{
    ABoxEc800AtPort at_port = {0};
    MQTT_CoreConfig_t core = {0};
    ABoxMqttConfig config = {.host = "broker.example", .port = 1883U,
                            .username = "user", .password = "pass"};
    at_port.tick_ms = tick;
    at_port.write = write_bytes;
    assert(ABoxEc800At_Init(&at, &at_port));
    strcpy(core.client_id, "patrol-1");
    strcpy(core.sub_topics[0], "a/b");
    core.sub_topic_count = 1U;
    MQTT_Core_Init(&core, message);
    MQTT_Core_SetNetworkChangedCallback(network_changed);
    MQTT_Core_SetAtCore(&at);
    assert(MQTT_Core_ApplyRuntimeConfig(&config));
    MQTT_Core_SetSecurityReady(1U);
    MQTT_Core_SetPaused(0U);
    answer("ATE0", "OK\r\n");
    answer("AT+QMTCFG", "OK\r\n");
    answer("AT+CEREG?", "+CEREG: 0,1\r\nOK\r\n");
    answer("AT+CGATT?", "+CGATT: 1\r\nOK\r\n");
    answer("AT+QIACT?", "+QIACT: 1,1,1\r\nOK\r\n");
    answer("AT+QMTOPEN", "OK\r\n+QMTOPEN: 0,0\r\n");
    answer("AT+QMTCONN", "OK\r\n+QMTCONN: 0,0,0\r\n");
    answer("AT+QMTSUB", "OK\r\n+QMTSUB: 0,1,0,1\r\n");
    cycle();
    assert(MQTT_Core_IsReady());
    feed("+QMTRECV: 0,0,\"a/b\",4,test\r\n");
    assert(messages == 1U && !strcmp(last_topic, "a/b") &&
           !strcmp(last_payload, "test"));
    feed("+QMTSTAT: 0,1\r\n");
    cycle();
    assert(!MQTT_Core_IsReady() && revoked == 1U);
    MQTT_Core_SetPaused(1U);
    assert(MQTT_Core_Stop(now_ms) == 0);
    answer("AT+QMTDISC", "+QMTDISC: 0,0\r\n");
    answer("AT+QMTCLOSE", "+QMTCLOSE: 0,0\r\n");
    now_ms += 1500U;
    cycle();
    assert(MQTT_Core_Stop(now_ms) == 1);
    assert(MQTT_Core_BorrowWorkspace());
    MQTT_Core_ReturnWorkspace();
    MQTT_Core_SetPaused(0U);
    assert(!MQTT_Core_IsReady());
    cycle();
    assert(!strncmp(command, "ATE0", 4U));
    puts("mqtt_adapter_test passed");
    return 0;
}
