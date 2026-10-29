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
// 把公共 EC800 MQTT 状态映射到产品兼容状态。维护适配器状态视图与就绪事件，不在回调中重新实现连接状态机。
static void state_changed(void *context, ABoxMqttEc800State state)
{ (void)context; if (state == ABOX_MQTT_EC800_READY) ++g_ready_sequence; }
static void (*g_network_changed)(void);
static void (*g_modem_reset)(void);
// 转发公共 MQTT 核心的模块复位通知。调用已经注册的产品回调，未绑定时不执行额外动作。
static void modem_reset(void *context)
{ (void)context; if (g_modem_reset) g_modem_reset(); }
static uint8_t g_header[128], g_topic[TOPIC_CAPACITY];
static uint8_t g_payload[PAYLOAD_CAPACITY], g_outgoing[1536U];

// 转发已经完整接收的 MQTT Topic 和载荷。由统一云任务上下文处理，回调使用者不得假定传入指针在回调返回后仍保存原消息。
static void message(void *context, const char *topic, const char *payload)
{
    (void)context;
    if (g_message) g_message(topic, payload);
}

// 转发 MQTT 网络配置变化通知。通知产品撤销旧连接授权及发送关联，不自动恢复底盘会话。
static void network_changed(void *context)
{
    (void)context;
    if (g_network_changed) g_network_changed();
}

// 把兼容层配置整理为公共 MQTT 核心配置。填写 Broker、Client ID、订阅与事务超时，字符串引用静态配置存储；缓冲区与回调由 SetAtCore 另行绑定。
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

// 初始化产品 MQTT 兼容适配器。复制配置并保存消息回调，清空旧 bridge 和连接序号；实际 AT 核心与存储绑定在 SetAtCore 中完成。
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
    g_modem_reset = 0;
    memset(&g_bridge, 0, sizeof(g_bridge));
}

// 绑定共享 EC800 AT 核心。多个服务通过统一事务与工作区约束使用同一核心，不能在别的任务中任意并发推进。
void MQTT_Core_SetAtCore(Ec800AtCore_t *at)
{
    ABoxMqttEc800Buffers buffers;
    ABoxMqttEc800Config config;
    static const ABoxMqttEc800BridgeHooks hooks = {
        .message = message,
        .state_changed = state_changed,
        .network_changed = network_changed,
        .modem_reset = modem_reset
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

// 重置 MQTT bridge 的连接与传输状态。通过公共核心完成复位，不将本地缓存状态当作模块仍保持连接的证据。
void MQTT_Core_Reset(void)
{
    ABoxMqttEc800Bridge_OnModemReset(&g_bridge, HAL_GetTick());
}

// 推进公共 MQTT bridge 一次。应由统一云调度调用，处理有限状态和回执，不另建产品私有连接状态机。
void MQTT_Core_Task(void)
{
    ABoxMqttEc800Bridge_Poll(&g_bridge, HAL_GetTick());
}

// 请求公共核心按有序流程重新连接。请求是异步的，真正恢复须等待后续连接、订阅和就绪反馈。
void MQTT_Core_RequestReconnect(void)
{
    if (g_bridge.initialized)
        ABoxMqttEc800_RequestReconnect(&g_bridge.transport, HAL_GetTick());
}
// 请求公共核心强制重建 MQTT 连接。用于既有恢复路径，可能使在途发送失效；相关业务应根据网络变更事件清理关联。
void MQTT_Core_ForceReconnect(void) { MQTT_Core_RequestReconnect(); }

// 设置 MQTT 连接与后台轮询暂停标记。用于注册或维护占用共享 AT 路径，暂停不等于模块已经断开网络。
void MQTT_Core_SetPaused(uint8_t paused)
{
    ABoxMqttEc800Bridge_SetPaused(&g_bridge, paused, HAL_GetTick());
}
// 通知核心 TLS 安全前置资源是否可用。未就绪时不得依靠普通发布路径绕过安全连接前提。
void MQTT_Core_SetSecurityReady(uint8_t ready)
{
    ABoxMqttEc800Bridge_SetSecurityReady(&g_bridge, ready);
}
// 注册网络变化时调用的产品回调。回调应处理旧连接授权和缓存失效，不应在其中阻塞等待网络重连。
void MQTT_Core_SetNetworkChangedCallback(void (*callback)(void))
{
    g_network_changed = callback;
}
// 注册模块复位时调用的产品回调。用于使共享 AT、注册或传输的旧状态失效，空回调按核心约定处理。
void MQTT_Core_SetModemResetCallback(void (*callback)(void))
{
    g_modem_reset = callback;
}

// 有序停止当前 MQTT 运行连接。返回公共核心的停止进度结果，具体完成必须结合状态；不是立即断电接口。
int MQTT_Core_Stop(uint32_t now_ms)
{
    return g_bridge.initialized ?
        ABoxMqttEc800_Stop(&g_bridge.transport, now_ms) : -1;
}
// 尝试独占借用 MQTT 与 OTA 共用的工作区。返回借用结果；成功后调用者必须按约定归还，避免静态 RAM 工作区被并发覆盖。
int MQTT_Core_BorrowWorkspace(void)
{
    return ABoxMqttEc800Bridge_BorrowWorkspace(&g_bridge, 1024U);
}
// 归还先前借用的共享工作区。仅在借用者完成当前使用后调用，归还不表示 OTA 或 MQTT 事务已成功。
void MQTT_Core_ReturnWorkspace(void)
{
    ABoxMqttEc800Bridge_ReturnWorkspace(&g_bridge);
}
// 取得可供 OTA 使用的工作区及容量。输出 capacity 由核心维护，调用者须遵守借用协议，不能越过返回容量写入。
uint8_t *MQTT_Core_OtaWorkspace(uint32_t *capacity)
{
    if (capacity) *capacity = sizeof(g_payload);
    return g_payload;
}

// 在容量限制内复制配置文本并保证终止。用于适配器持有配置字符串，避免直接保留短生命周期调用方文本。
static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t n;
    if (!source) return;
    n = strlen(source);
    if (n >= capacity) n = capacity - 1U;
    memcpy(destination, source, n);
    destination[n] = 0;
}
// 更新兼容层缓存的 Broker 地址和认证字段。复制传入文本，非零端口才替换旧值；仅修改配置缓存，不在此启动重连或确认新 Broker 可用。
void MQTT_Core_UpdateBroker(const char *host, uint16_t port,
                            const char *username, const char *password)
{
    copy_text(g_config.server_host, sizeof(g_config.server_host), host);
    if (port) g_config.server_port = port;
    copy_text(g_config.username, sizeof(g_config.username), username);
    copy_text(g_config.password, sizeof(g_config.password), password);
}
// 应用完整运行时 MQTT 配置到公共核心。复用平台配置校验和连接切换语义，返回配置应用结果，试连证明由更高层管理。
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
// 设置当前 MQTT Client ID。使用核心容量和状态约束，不允许调用方以未经校验的 VID 绕过产品身份检查。
uint8_t MQTT_Core_SetClientId(const char *client_id)
{
    if (!client_id || !*client_id ||
        strlen(client_id) >= sizeof(g_config.client_id)) return 0U;
    copy_text(g_config.client_id, sizeof(g_config.client_id), client_id);
    return 1U;
}
// 更新 MQTT 订阅集合和默认发布 Topic。调用方须提供有效的 Topic 存储，真正订阅完成由后续就绪状态确认。
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
// 查询当前连接是否满足业务发布就绪条件。与仅 TCP 或 MQTT 已连接不同，使用公共核心提供的完整就绪判断。
uint8_t MQTT_Core_IsReady(void)
{
    return g_bridge.initialized && ABoxMqttEc800_IsReady(&g_bridge.transport);
}
// 查询 MQTT 协议连接是否已建立。连接存在不代表订阅完成或当前发布已收到回执。
uint8_t MQTT_Core_IsConnected(void)
{
    return g_bridge.initialized && ABoxMqttEc800_IsConnected(&g_bridge.transport);
}
// 查询当前是否允许后台 AT 查询占用通道。在发布、接收或维护事务占用时由核心限制，避免打断共享解析器状态。
uint8_t MQTT_Core_CanRunBackgroundAt(void)
{
    return (uint8_t)ABoxMqttEc800Bridge_CanRunBackgroundAt(&g_bridge);
}
// 读取兼容层 MQTT 状态枚举。用于诊断和恢复调度，调用者不得自行改写适配器内部状态。
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
// 查询是否存在在途 MQTT 发布。用于串行化共享发送缓冲和业务回执，不能将非活动解释为上条消息一定成功。
uint8_t MQTT_Core_IsPublishActive(void)
{
    return g_bridge.initialized && g_bridge.transport.publish_active;
}
// 取得已完成发布的递增成功序号。用于观察新发布证明，单条业务结果仍应使用 operation 关联回执。
uint32_t MQTT_Core_GetPubOkSeq(void) { return g_bridge.publish_ok; }
// 按默认非 retain 方式提交 MQTT 消息。返回是否被核心接受，载荷应在约定生命周期内有效；接受不等于 Broker 或业务端完成处理。
uint8_t MQTT_Core_Publish(const char *topic, const char *payload, uint8_t qos)
{
    return MQTT_Core_PublishEx(topic, payload, qos, 0U);
}
// 提交显式 QoS 和 retain 的 MQTT 消息。复用公共核心发送接口，不额外创建私有重试队列；发布完成由后续状态决定。
uint8_t MQTT_Core_PublishEx(const char *topic, const char *payload,
                           uint8_t qos, uint8_t retain)
{
    uint64_t operation;
    return MQTT_Core_PublishExTracked(topic, payload, qos, retain, &operation);
}
// 提交消息并取得可追踪的发布操作号。仅在提交成功后按返回的 operation 查询回执，避免其他消息完成事件串扰当前业务。
uint8_t MQTT_Core_PublishExTracked(const char *topic, const char *payload,
                                  uint8_t qos, uint8_t retain,
                                  uint64_t *operation)
{
    return (uint8_t)ABoxMqttEc800Bridge_Publish(&g_bridge, topic, payload,
                                                 qos, retain, operation);
}
// 查询指定发布操作的回执枚举。区分在途、成功、失败或无效操作，不以任意成功序号替代准确操作关联。
ABoxMqttReceiptStatus MQTT_Core_PublishResult(uint64_t operation)
{
    return ABoxMqttReceipt_Get(&g_bridge.receipt, operation);
}
// 取消当前尚待完成的发布事务。取消会使对应业务等待失效，上层负责清理载荷和 operation 的关联状态。
void MQTT_Core_CancelPendingPublish(void)
{
    ABoxMqttEc800Bridge_CancelPublish(&g_bridge);
}
// 安排读取 SIM ICCID 的后台 AT 请求。需由统一核心在可用时机执行，不在调用点阻塞等待查询结果。
void MQTT_Core_RequestIccid(void)
{
    (void)ABoxMqttEc800Bridge_RequestIccid(&g_bridge, HAL_GetTick());
}
// 查询 ICCID 缓存是否已有有效结果。仅反映缓存有效性，不作为网络就绪或设备注册成功的证明。
uint8_t MQTT_Core_IsIccidValid(void) { return g_bridge.iccid.valid; }
// 取得核心保存的 ICCID 文本。返回内部存储指针，读取前应检查有效标志，调用者不得修改或释放。
const char *MQTT_Core_GetIccid(void)
{
    return ABoxEc800Iccid_Value(&g_bridge.iccid);
}
// 查询 ICCID 请求是否仍在排队或进行。用于后台查询节流，避免每轮重复提交相同 AT 请求。
uint8_t MQTT_Core_IsIccidPending(void) { return g_bridge.iccid.pending; }
// 查询核心是否正在收集 MQTT 接收载荷。用于共享 AT 通道避让，不应在该阶段直接注入不相关命令。
uint8_t MQTT_Core_IsRxCollecting(void)
{
    return g_bridge.initialized &&
           ABoxMqttEc800Rx_IsCollecting(&g_bridge.transport.rx);
}
// 复制 MQTT 接收路径诊断状态。输出累计错误和接收上下文，供内部查询，不改变接收事务。
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

// 返回公共 EC800 MQTT bridge 实例。供平台 OTA 适配器绑定共享传输，指针生命周期由本模块维护，不得释放或另起并行调度。
ABoxMqttEc800Bridge *MQTT_Core_Bridge(void) { return &g_bridge; }

// 读取 MQTT 进入就绪阶段的事件序号。用于识别新的连接周期，不把旧周期就绪事件作为新配置已验证的证明。
uint32_t MQTT_Core_GetReadySeq(void) { return g_ready_sequence; }
