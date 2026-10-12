#include "mqtt_core.h"

#include "abox_mqtt_ec800_bridge.h"
#include "app_log.h"
#include "main.h"
#include <string.h>

/* Legacy MQTT_Core API for the product. The standard EC800 session, receipt,
 * ICCID and workspace lifecycle are owned by Platform. */
enum { TOPIC_CAPACITY = 72U, PAYLOAD_CAPACITY = 1152U };
/* Keep only configuration consumed by the bridge. The public API's default
 * publish topic is unused because every publish supplies its own topic. */
static struct {
    uint8_t client_idx;
    char server_host[64];
    uint16_t server_port;
    char client_id[32], username[32], password[64];
    char sub_topics[4][72];
    uint8_t sub_topic_count;
} g_config;
static ABoxMqttEc800Bridge g_bridge;
static const char *g_subscriptions[4];
static MQTT_CoreMsgCallback_t g_message;
static uint32_t g_ready_sequence;
static void state_changed(void *context, ABoxMqttEc800State state)
{ (void)context; if (state == ABOX_MQTT_EC800_READY) ++g_ready_sequence; }
static void (*g_network_changed)(void);
static uint8_t g_header[128], g_topic[TOPIC_CAPACITY];
static uint8_t g_payload[PAYLOAD_CAPACITY], g_outgoing[1536U];

static void message(void *context, const char *topic, const char *payload)
{
    (void)context;
    if (g_message) g_message(topic, payload);
}

static void network_changed(void *context)
{
    (void)context;
    if (g_network_changed) g_network_changed();
}

static void build_config(ABoxMqttEc800Config *config)
{
    uint8_t i;
    memset(config, 0, sizeof(*config));
    config->apn = "CMNET";
    config->host = g_config.server_host;
    config->port = g_config.server_port;
    config->client_id = g_config.client_id;
    config->username = g_config.username;
    config->password = g_config.password;
    config->client_index = g_config.client_idx;
    config->subscription_count = g_config.sub_topic_count;
    for (i = 0U; i < 4U; ++i)
        g_subscriptions[i] = g_config.sub_topics[i];
    config->subscriptions = g_subscriptions;
    config->command_timeout_ms = 5000U;
    config->open_timeout_ms = 125000U;
    config->connect_timeout_ms = 10000U;
    config->subscribe_timeout_ms = 5000U;
    config->publish_timeout_ms = 15000U;
    config->retry_delay_ms = 4000U;
    config->legacy_json = 1U;
}

void MQTT_Core_Init(const MQTT_CoreConfig_t *config, MQTT_CoreMsgCallback_t callback)
{
    memset(&g_config, 0, sizeof(g_config));
    if (config) {
        g_config.client_idx = config->client_idx;
        g_config.server_port = config->server_port;
        g_config.sub_topic_count = config->sub_topic_count;
        memcpy(g_config.server_host, config->server_host, sizeof(g_config.server_host));
        memcpy(g_config.client_id, config->client_id, sizeof(g_config.client_id));
        memcpy(g_config.username, config->username, sizeof(g_config.username));
        memcpy(g_config.password, config->password, sizeof(g_config.password));
        memcpy(g_config.sub_topics, config->sub_topics, sizeof(g_config.sub_topics));
    }
    g_message = callback;
    g_ready_sequence = 0U;
    g_network_changed = 0;
    memset(&g_bridge, 0, sizeof(g_bridge));
}

void MQTT_Core_SetAtCore(Ec800AtCore_t *at)
{
    ABoxMqttEc800Buffers buffers;
    ABoxMqttEc800Config config;
    static const ABoxMqttEc800BridgeHooks hooks = {
        .message = message,
        .state_changed = state_changed,
        .network_changed = network_changed
    };
    if (!at) return;
    memset(&buffers, 0, sizeof(buffers));
    buffers.header = g_header; buffers.header_capacity = sizeof(g_header);
    buffers.topic = g_topic; buffers.topic_capacity = sizeof(g_topic);
    buffers.payload = g_payload; buffers.payload_capacity = sizeof(g_payload);
    buffers.publish_payload = g_outgoing;
    buffers.publish_capacity = sizeof(g_outgoing);
    /* Init copies this descriptor; its strings and subscriptions stay static. */
    build_config(&config);
    if (!ABoxMqttEc800Bridge_Init(&g_bridge, at, &config,
                                   &buffers, &hooks,
                                   ABOX_EC800_OWNER_PRODUCT_BASE + 5U,
                                   HAL_GetTick()))
        APP_LOGE("MQTT", "public transport init failed");
}

void MQTT_Core_Reset(void)
{
    ABoxMqttEc800Bridge_OnModemReset(&g_bridge, HAL_GetTick());
}

void MQTT_Core_Task(void)
{
    ABoxMqttEc800Bridge_Poll(&g_bridge, HAL_GetTick());
}

void MQTT_Core_RequestReconnect(void)
{
    if (g_bridge.initialized)
        ABoxMqttEc800_RequestReconnect(&g_bridge.transport, HAL_GetTick());
}
void MQTT_Core_ForceReconnect(void) { MQTT_Core_RequestReconnect(); }

void MQTT_Core_SetPaused(uint8_t paused)
{
    ABoxMqttEc800Bridge_SetPaused(&g_bridge, paused, HAL_GetTick());
}
void MQTT_Core_SetSecurityReady(uint8_t ready)
{
    ABoxMqttEc800Bridge_SetSecurityReady(&g_bridge, ready);
}
void MQTT_Core_SetNetworkChangedCallback(void (*callback)(void))
{
    g_network_changed = callback;
}

int MQTT_Core_Stop(uint32_t now_ms)
{
    return g_bridge.initialized ?
        ABoxMqttEc800_Stop(&g_bridge.transport, now_ms) : -1;
}
int MQTT_Core_BorrowWorkspace(void)
{
    return ABoxMqttEc800Bridge_BorrowWorkspace(&g_bridge, 1024U);
}
void MQTT_Core_ReturnWorkspace(void)
{
    ABoxMqttEc800Bridge_ReturnWorkspace(&g_bridge);
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
}
uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig *config)
{
    ABoxMqttEc800Config transport_config;
    if (!config || !config->host || !config->username ||
        !config->password || !config->port || !g_bridge.initialized ||
        strlen(config->host) >= sizeof(g_config.server_host) ||
        strlen(config->username) >= sizeof(g_config.username) ||
        strlen(config->password) >= sizeof(g_config.password))
        return 0U;
    if (ABoxMqttEc800_GetState(&g_bridge.transport) != ABOX_MQTT_EC800_IDLE &&
        ABoxMqttEc800_GetState(&g_bridge.transport) != ABOX_MQTT_EC800_PAUSED)
        return 0U;
    MQTT_Core_UpdateBroker(config->host, config->port,
                           config->username, config->password);
    g_config.client_idx = config->tls_enabled ? 1U : 0U;
    build_config(&transport_config);
    return (uint8_t)ABoxMqttEc800Bridge_Configure(&g_bridge,
                                                    &transport_config);
}
uint8_t MQTT_Core_SetClientId(const char *client_id)
{
    if (!client_id || !*client_id ||
        strlen(client_id) >= sizeof(g_config.client_id)) return 0U;
    copy_text(g_config.client_id, sizeof(g_config.client_id), client_id);
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
    (void)pub_topic;
}
uint8_t MQTT_Core_IsReady(void)
{
    return g_bridge.initialized && ABoxMqttEc800_IsReady(&g_bridge.transport);
}
uint8_t MQTT_Core_IsConnected(void)
{
    return g_bridge.initialized && ABoxMqttEc800_IsConnected(&g_bridge.transport);
}
uint8_t MQTT_Core_CanRunBackgroundAt(void)
{
    return (uint8_t)ABoxMqttEc800Bridge_CanRunBackgroundAt(&g_bridge);
}
MQTT_CoreState_t MQTT_Core_GetState(void)
{
    switch (ABoxMqttEc800_GetState(&g_bridge.transport)) {
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
    return g_bridge.initialized && g_bridge.transport.publish_active;
}
uint32_t MQTT_Core_GetPubOkSeq(void) { return g_bridge.publish_ok; }
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
                                  uint8_t qos, uint8_t retain,
                                  uint64_t *operation)
{
    return (uint8_t)ABoxMqttEc800Bridge_Publish(&g_bridge, topic, payload,
                                                 qos, retain, operation);
}
ABoxMqttReceiptStatus MQTT_Core_PublishResult(uint64_t operation)
{
    return ABoxMqttReceipt_Get(&g_bridge.receipt, operation);
}
void MQTT_Core_CancelPendingPublish(void)
{
    ABoxMqttEc800Bridge_CancelPublish(&g_bridge);
}
void MQTT_Core_RequestIccid(void)
{
    (void)ABoxMqttEc800Bridge_RequestIccid(&g_bridge, HAL_GetTick());
}
uint8_t MQTT_Core_IsIccidValid(void) { return g_bridge.iccid.valid; }
const char *MQTT_Core_GetIccid(void)
{
    return ABoxEc800Iccid_Value(&g_bridge.iccid);
}
uint8_t MQTT_Core_IsIccidPending(void) { return g_bridge.iccid.pending; }
uint8_t MQTT_Core_IsRxCollecting(void)
{
    return g_bridge.initialized &&
           ABoxMqttEc800Rx_IsCollecting(&g_bridge.transport.rx);
}
void MQTT_Core_GetRxDiagnostics(MQTT_CoreRxDiagnostics_t *diagnostics)
{
    ABoxMqttEc800Counters counters;
    if (!diagnostics) return;
    memset(diagnostics, 0, sizeof(*diagnostics));
    if (!g_bridge.initialized) return;
    ABoxMqttEc800_GetCounters(&g_bridge.transport, &counters);
    diagnostics->decoded_message_count = counters.receive.completed;
    diagnostics->rejected_frame_count = counters.receive.rejected;
    diagnostics->multiline_timeout_count = counters.receive.expired;
}

ABoxMqttEc800Bridge *MQTT_Core_Bridge(void) { return &g_bridge; }

uint32_t MQTT_Core_GetReadySeq(void) { return g_ready_sequence; }
