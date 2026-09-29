#include "mqtt_core.h"

#include "abox_mqtt_ec800.h"
#include "abox_ec800_iccid.h"
#include "abox_mqtt_receipt.h"
#include "app_log.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Product identity and Topic storage stay here. Connection, AT command
 * progression and RX framing are owned by ABoxMqttEc800. */
enum { TOPIC_CAPACITY = 128U, PAYLOAD_CAPACITY = 1152U };
static MQTT_CoreConfig_t g_config;
static ABoxMqttEc800 g_transport;
static ABoxMqttReceipt g_receipt;
static ABoxMqttEc800Config g_transport_config;
static const char *g_subscriptions[4];
static Ec800AtCore_t *g_at;
static MQTT_CoreMsgCallback_t g_message;
static uint8_t g_header[192], g_topic[TOPIC_CAPACITY];
static uint8_t g_payload[PAYLOAD_CAPACITY], g_outgoing[1536U];
static ABoxEc800Iccid g_iccid;
static uint32_t g_publish_ok;
static uint8_t g_initialized, g_security_ready, g_paused;
static void (*g_network_changed)(void);
static ABoxMqttEc800State g_last_state;

static void message(void *user, const uint8_t *topic, size_t topic_length,
                    const uint8_t *payload, size_t payload_length)
{
    (void)user;
    if (!g_message || topic_length >= sizeof(g_topic) ||
        payload_length >= sizeof(g_payload) ||
        memchr(topic, 0, topic_length) || memchr(payload, 0, payload_length))
        return;
    g_topic[topic_length] = 0;
    g_payload[payload_length] = 0;
    g_message((const char *)topic, (const char *)payload);
}

static void publish_event(void *user, uint64_t operation,
                          ABoxMqttEc800PublishEvent event)
{
    (void)user;
    ABoxMqttReceipt_OnEvent(&g_receipt, operation, event);
    if (event == ABOX_MQTT_EC800_PUBLISH_CONFIRMED) ++g_publish_ok;
}
static void state_changed(void *user, ABoxMqttEc800State state)
{
    (void)user;
    if (g_last_state == ABOX_MQTT_EC800_READY &&
        state != ABOX_MQTT_EC800_READY && g_network_changed)
        g_network_changed();
    g_last_state = state;
}

static void build_config(void)
{
    uint8_t i;
    memset(&g_transport_config, 0, sizeof(g_transport_config));
    g_transport_config.apn = "CMNET";
    g_transport_config.host = g_config.server_host;
    g_transport_config.port = g_config.server_port;
    g_transport_config.client_id = g_config.client_id;
    g_transport_config.username = g_config.username;
    g_transport_config.password = g_config.password;
    g_transport_config.client_index = g_config.client_idx;
    g_transport_config.subscription_count = g_config.sub_topic_count;
    for (i = 0U; i < 4U; ++i)
        g_subscriptions[i] = g_config.sub_topics[i];
    g_transport_config.subscriptions = g_subscriptions;
    g_transport_config.command_timeout_ms = 5000U;
    g_transport_config.open_timeout_ms = 125000U;
    g_transport_config.connect_timeout_ms = 10000U;
    g_transport_config.subscribe_timeout_ms = 5000U;
    g_transport_config.publish_timeout_ms = 15000U;
    g_transport_config.retry_delay_ms = 4000U;
    g_transport_config.legacy_json = 1U;
}

void MQTT_Core_Init(const MQTT_CoreConfig_t *config, MQTT_CoreMsgCallback_t callback)
{
    memset(&g_config, 0, sizeof(g_config));
    if (config) g_config = *config;
    g_message = callback;
    memset(&g_iccid, 0, sizeof(g_iccid));
    g_publish_ok = 0U;
    ABoxMqttReceipt_Init(&g_receipt);
    g_security_ready = g_paused = g_initialized = 0U;
    g_last_state = ABOX_MQTT_EC800_IDLE;
    build_config();
}

void MQTT_Core_SetAtCore(Ec800AtCore_t *at)
{
    ABoxMqttEc800Buffers buffers;
    ABoxMqttEc800Callbacks callbacks;
    if (!at) return;
    g_at = at;
    memset(&buffers, 0, sizeof(buffers));
    memset(&callbacks, 0, sizeof(callbacks));
    buffers.header = g_header; buffers.header_capacity = sizeof(g_header);
    buffers.topic = g_topic; buffers.topic_capacity = sizeof(g_topic);
    buffers.payload = g_payload; buffers.payload_capacity = sizeof(g_payload);
    buffers.publish_payload = g_outgoing;
    buffers.publish_capacity = sizeof(g_outgoing);
    callbacks.message = message;
    callbacks.publish_event = publish_event;
    callbacks.state_changed = state_changed;
    if (!ABoxMqttEc800_Init(&g_transport, at, &g_transport_config,
                            &buffers, &callbacks, HAL_GetTick()) ||
        !ABoxEc800Iccid_Init(&g_iccid, at,
                             ABOX_EC800_OWNER_PRODUCT_BASE + 5U)) {
        APP_LOGE("MQTT", "public transport init failed");
        return;
    }
    g_initialized = 1U;
}

void MQTT_Core_Reset(void)
{
    if (!g_initialized) return;
    ABoxMqttReceipt_Invalidate(&g_receipt);
    ABoxMqttEc800_OnModemReset(&g_transport, HAL_GetTick());
    ABoxEc800Iccid_OnModemReset(&g_iccid);
}

void MQTT_Core_Task(void)
{
    if (!g_initialized) return;
    ABoxMqttEc800_Poll(&g_transport, HAL_GetTick());
    ABoxEc800Iccid_Poll(&g_iccid, HAL_GetTick());
}

void MQTT_Core_RequestReconnect(void)
{
    if (g_initialized)
        ABoxMqttEc800_RequestReconnect(&g_transport, HAL_GetTick());
}
void MQTT_Core_ForceReconnect(void) { MQTT_Core_RequestReconnect(); }

void MQTT_Core_SetPaused(uint8_t paused)
{
    g_paused = paused ? 1U : 0U;
    if (!g_initialized) return;
    ABoxMqttEc800_SetPausedSoft(&g_transport, g_paused);
    if (!g_paused && g_security_ready) {
        ABoxMqttEc800State state = ABoxMqttEc800_GetState(&g_transport);
        if (state == ABOX_MQTT_EC800_PAUSED)
            (void)ABoxMqttEc800_Resume(&g_transport, HAL_GetTick());
        else if (state == ABOX_MQTT_EC800_IDLE)
            (void)ABoxMqttEc800_Start(&g_transport, HAL_GetTick());
    }
}
void MQTT_Core_SetSecurityReady(uint8_t ready)
{
    g_security_ready = ready ? 1U : 0U;
    if (g_initialized)
        ABoxMqttEc800_SetSecurityReady(&g_transport, g_security_ready);
}
void MQTT_Core_SetNetworkChangedCallback(void (*callback)(void))
{
    g_network_changed = callback;
}

int MQTT_Core_Stop(uint32_t now_ms)
{
    return g_initialized ? ABoxMqttEc800_Stop(&g_transport, now_ms) : -1;
}
int MQTT_Core_BorrowWorkspace(void)
{
    size_t capacity = 0U;
    return g_initialized &&
           ABoxMqttEc800_BorrowWorkspace(&g_transport, &capacity) == g_payload &&
           capacity >= 1024U;
}
void MQTT_Core_ReturnWorkspace(void)
{
    if (g_initialized) (void)ABoxMqttEc800_ReturnWorkspace(&g_transport);
}
uint8_t *MQTT_Core_OtaWorkspace(uint32_t *capacity)
{
    if (capacity) *capacity = sizeof(g_payload);
    return g_payload;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t n;
    if (!source) return;
    n = strlen(source);
    if (n >= capacity) n = capacity - 1U;
    memcpy(destination, source, n);
    destination[n] = 0;
}
void MQTT_Core_UpdateBroker(const char *host, uint16_t port,
                            const char *username, const char *password)
{
    copy_text(g_config.server_host, sizeof(g_config.server_host), host);
    if (port) g_config.server_port = port;
    copy_text(g_config.username, sizeof(g_config.username), username);
    copy_text(g_config.password, sizeof(g_config.password), password);
    build_config();
}
uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig *config)
{
    ABoxMqttEc800State state;
    if (!config || !config->host || !config->username ||
        !config->password || !config->port || !g_initialized ||
        strlen(config->host) >= sizeof(g_config.server_host) ||
        strlen(config->username) >= sizeof(g_config.username) ||
        strlen(config->password) >= sizeof(g_config.password))
        return 0U;
    state = ABoxMqttEc800_GetState(&g_transport);
    if (state != ABOX_MQTT_EC800_IDLE &&
        state != ABOX_MQTT_EC800_PAUSED) return 0U;
    MQTT_Core_UpdateBroker(config->host, config->port,
                           config->username, config->password);
    g_config.client_idx = config->tls_enabled ? 1U : 0U;
    build_config();
    return ABoxMqttEc800_Configure(&g_transport, &g_transport_config);
}
uint8_t MQTT_Core_SetClientId(const char *client_id)
{
    if (!client_id || !*client_id ||
        strlen(client_id) >= sizeof(g_config.client_id)) return 0U;
    copy_text(g_config.client_id, sizeof(g_config.client_id), client_id);
    build_config();
    return 1U;
}
void MQTT_Core_SetTopics(const char *const *topics, uint8_t count,
                         const char *pub_topic)
{
    uint8_t i;
    if (count > 4U) return;
    memset(g_config.sub_topics, 0, sizeof(g_config.sub_topics));
    for (i = 0U; i < count; ++i)
        copy_text(g_config.sub_topics[i], sizeof(g_config.sub_topics[i]),
                  topics[i]);
    g_config.sub_topic_count = count;
    copy_text(g_config.pub_topic, sizeof(g_config.pub_topic), pub_topic);
    build_config();
}
uint8_t MQTT_Core_IsReady(void)
{
    return g_initialized && ABoxMqttEc800_IsReady(&g_transport);
}
uint8_t MQTT_Core_IsConnected(void)
{
    return g_initialized && ABoxMqttEc800_IsConnected(&g_transport);
}
uint8_t MQTT_Core_CanRunBackgroundAt(void)
{
    return MQTT_Core_IsReady() && !g_transport.publish_active &&
           !ABoxEc800At_IsBusy(g_at) &&
           !ABoxMqttEc800Rx_IsCollecting(&g_transport.rx);
}
MQTT_CoreState_t MQTT_Core_GetState(void)
{
    switch (ABoxMqttEc800_GetState(&g_transport)) {
    case ABOX_MQTT_EC800_READY: return MQTT_CORE_ST_READY;
    case ABOX_MQTT_EC800_SUBSCRIBING: return MQTT_CORE_ST_WAIT_SUB;
    case ABOX_MQTT_EC800_CONNECTING: return MQTT_CORE_ST_WAIT_CONN;
    case ABOX_MQTT_EC800_OPENING: return MQTT_CORE_ST_WAIT_OPEN;
    case ABOX_MQTT_EC800_CONFIGURING: return MQTT_CORE_ST_CFG;
    case ABOX_MQTT_EC800_CLOSING:
    case ABOX_MQTT_EC800_RETRY_WAIT: return MQTT_CORE_ST_RECONNECT_WAIT;
    case ABOX_MQTT_EC800_BLOCKED: return MQTT_CORE_ST_ERROR;
    default: return MQTT_CORE_ST_IDLE;
    }
}
uint8_t MQTT_Core_IsPublishActive(void)
{
    return g_initialized && g_transport.publish_active;
}
uint32_t MQTT_Core_GetPubOkSeq(void) { return g_publish_ok; }
uint8_t MQTT_Core_Publish(const char *topic, const char *payload, uint8_t qos)
{
    return MQTT_Core_PublishEx(topic, payload, qos, 0U);
}
uint8_t MQTT_Core_PublishEx(const char *topic, const char *payload,
                           uint8_t qos, uint8_t retain)
{
    uint64_t operation;
    return MQTT_Core_PublishExTracked(topic, payload, qos, retain, &operation);
}
uint8_t MQTT_Core_PublishExTracked(const char *topic, const char *payload,
                                  uint8_t qos, uint8_t retain, uint64_t *operation)
{
    if (!g_initialized || !topic || !payload || !operation ||
        g_receipt.status == ABOX_MQTT_RECEIPT_PENDING ||
        !ABoxMqttEc800_Publish(&g_transport, topic,
                               (const uint8_t *)payload, strlen(payload),
                               qos, retain, operation)) return 0U;
    return (uint8_t)ABoxMqttReceipt_Begin(&g_receipt, *operation);
}
ABoxMqttReceiptStatus MQTT_Core_PublishResult(uint64_t operation)
{
    return ABoxMqttReceipt_Get(&g_receipt, operation);
}
void MQTT_Core_CancelPendingPublish(void)
{
    if (g_initialized) ABoxMqttEc800_CancelPublish(&g_transport);
}
void MQTT_Core_RequestIccid(void)
{
    if (MQTT_Core_CanRunBackgroundAt())
        (void)ABoxEc800Iccid_Request(&g_iccid, HAL_GetTick());
}
uint8_t MQTT_Core_IsIccidValid(void) { return g_iccid.valid; }
const char *MQTT_Core_GetIccid(void) { return ABoxEc800Iccid_Value(&g_iccid); }
uint8_t MQTT_Core_IsIccidPending(void) { return g_iccid.pending; }
uint8_t MQTT_Core_IsRxCollecting(void)
{
    return g_initialized && ABoxMqttEc800Rx_IsCollecting(&g_transport.rx);
}
void MQTT_Core_GetRxDiagnostics(MQTT_CoreRxDiagnostics_t *diagnostics)
{
    ABoxMqttEc800Counters counters;
    if (!diagnostics) return;
    memset(diagnostics, 0, sizeof(*diagnostics));
    if (!g_initialized) return;
    ABoxMqttEc800_GetCounters(&g_transport, &counters);
    diagnostics->decoded_message_count = counters.receive.completed;
    diagnostics->rejected_frame_count = counters.receive.rejected;
    diagnostics->multiline_timeout_count = counters.receive.expired;
}
