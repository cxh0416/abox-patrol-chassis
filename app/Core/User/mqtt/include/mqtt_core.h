#include "abox_mqtt_ec800_bridge.h"
#ifndef __MQTT_CORE_H__
#define __MQTT_CORE_H__

#include <stdint.h>

#include "abox_ec800_at_legacy.h"
#include "abox_mqtt_trial.h"
#include "abox_mqtt_receipt.h"

typedef enum
{
    MQTT_CORE_ST_IDLE = 0,
    MQTT_CORE_ST_WAIT_BOOT,
    MQTT_CORE_ST_ECHO_OFF,
    MQTT_CORE_ST_WAIT_ECHO_OFF,
    MQTT_CORE_ST_CFG,
    MQTT_CORE_ST_WAIT_CFG,
    MQTT_CORE_ST_DIAG,
    MQTT_CORE_ST_WAIT_DIAG,
    MQTT_CORE_ST_NETWORK_QUERY,
    MQTT_CORE_ST_WAIT_NETWORK_QUERY,
    MQTT_CORE_ST_ATTACH_QUERY,
    MQTT_CORE_ST_WAIT_ATTACH_QUERY,
    MQTT_CORE_ST_ATTACH,
    MQTT_CORE_ST_WAIT_ATTACH,
    MQTT_CORE_ST_NETWORK_WAIT,
    MQTT_CORE_ST_PDP_QUERY,
    MQTT_CORE_ST_WAIT_PDP_QUERY,
    MQTT_CORE_ST_PDP_CONFIG,
    MQTT_CORE_ST_WAIT_PDP_CONFIG,
    MQTT_CORE_ST_PDP_ACTIVATE,
    MQTT_CORE_ST_WAIT_PDP_ACTIVATE,
    MQTT_CORE_ST_PDP_VERIFY,
    MQTT_CORE_ST_WAIT_PDP_VERIFY,
    MQTT_CORE_ST_OPEN,
    MQTT_CORE_ST_WAIT_OPEN,
    MQTT_CORE_ST_CONN,
    MQTT_CORE_ST_WAIT_CONN,
    MQTT_CORE_ST_SUB,
    MQTT_CORE_ST_WAIT_SUB,
    MQTT_CORE_ST_READY,
    MQTT_CORE_ST_RECOVER_PUB,
    MQTT_CORE_ST_RECONNECT_WAIT,
    MQTT_CORE_ST_ERROR
} MQTT_CoreState_t;

typedef struct
{
    uint8_t client_idx;
    char server_host[64];
    uint16_t server_port;
    char client_id[32];
    char username[32];
    char password[64];
    char sub_topics[4][72];
    uint8_t sub_topic_count;
    char pub_topic[72];
} MQTT_CoreConfig_t;

typedef void (*MQTT_CoreMsgCallback_t)(const char* topic, const char* payload);

typedef struct
{
    uint32_t qmtrecv_line_count;
    uint32_t decoded_message_count;
    uint32_t rejected_frame_count;
    uint32_t dispatched_message_count;
    uint32_t pending_overwrite_count;
    uint32_t multiline_timeout_count;
    uint32_t last_qmtrecv_tick;
} MQTT_CoreRxDiagnostics_t;

// 初始化产品 MQTT 兼容适配器。复制配置并保存消息回调，清空旧 bridge 和连接序号；实际 AT 核心与存储绑定在 SetAtCore 中完成。
void MQTT_Core_Init(const MQTT_CoreConfig_t* cfg, MQTT_CoreMsgCallback_t cb);
// 绑定共享 EC800 AT 核心。多个服务通过统一事务与工作区约束使用同一核心，不能在别的任务中任意并发推进。
void MQTT_Core_SetAtCore(Ec800AtCore_t* at_core);
// 重置 MQTT bridge 的连接与传输状态。通过公共核心完成复位，不将本地缓存状态当作模块仍保持连接的证据。
void MQTT_Core_Reset(void);
// 推进公共 MQTT bridge 一次。应由统一云调度调用，处理有限状态和回执，不另建产品私有连接状态机。
void MQTT_Core_Task(void);
// 请求公共核心按有序流程重新连接。请求是异步的，真正恢复须等待后续连接、订阅和就绪反馈。
void MQTT_Core_RequestReconnect(void);
// 请求公共核心强制重建 MQTT 连接。用于既有恢复路径，可能使在途发送失效；相关业务应根据网络变更事件清理关联。
void MQTT_Core_ForceReconnect(void);
// 设置 MQTT 连接与后台轮询暂停标记。用于注册或维护占用共享 AT 路径，暂停不等于模块已经断开网络。
void MQTT_Core_SetPaused(uint8_t paused);
// 通知核心 TLS 安全前置资源是否可用。未就绪时不得依靠普通发布路径绕过安全连接前提。
void MQTT_Core_SetSecurityReady(uint8_t ready);
// 注册网络变化时调用的产品回调。回调应处理旧连接授权和缓存失效，不应在其中阻塞等待网络重连。
void MQTT_Core_SetNetworkChangedCallback(void (*callback)(void));
// 注册模块复位时调用的产品回调。用于使共享 AT、注册或传输的旧状态失效，空回调按核心约定处理。
void MQTT_Core_SetModemResetCallback(void (*callback)(void));
/* Register the existing shared RAM with OTA at startup. The App OTA callback
 * must first stop MQTT and acquire the workspace lease; it returns the lease
 * before resuming MQTT. */
// 取得可供 OTA 使用的工作区及容量。输出 capacity 由核心维护，调用者须遵守借用协议，不能越过返回容量写入。
uint8_t* MQTT_Core_OtaWorkspace(uint32_t* capacity);
// 更新兼容层缓存的 Broker 地址和认证字段。复制传入文本，非零端口才替换旧值；仅修改配置缓存，不在此启动重连或确认新 Broker 可用。
void MQTT_Core_UpdateBroker(const char* host, uint16_t port, const char* username, const char* password);
/* Copies the normalized Platform runtime view. Product Flash layouts must be
 * mapped before this boundary. Security binding is completed by mqtt_runtime. */
// 应用完整运行时 MQTT 配置到公共核心。复用平台配置校验和连接切换语义，返回配置应用结果，试连证明由更高层管理。
uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig* config);
// 安排读取 SIM ICCID 的后台 AT 请求。需由统一核心在可用时机执行，不在调用点阻塞等待查询结果。
void MQTT_Core_RequestIccid(void);
// 更新 MQTT 订阅集合和默认发布 Topic。调用方须提供有效的 Topic 存储，真正订阅完成由后续就绪状态确认。
void MQTT_Core_SetTopics(const char* const* sub_topics, uint8_t sub_topic_count, const char* pub_topic);
// 设置当前 MQTT Client ID。使用核心容量和状态约束，不允许调用方以未经校验的 VID 绕过产品身份检查。
uint8_t MQTT_Core_SetClientId(const char* client_id);
// 查询当前连接是否满足业务发布就绪条件。与仅 TCP 或 MQTT 已连接不同，使用公共核心提供的完整就绪判断。
uint8_t MQTT_Core_IsReady(void);
// 查询 MQTT 协议连接是否已建立。连接存在不代表订阅完成或当前发布已收到回执。
uint8_t MQTT_Core_IsConnected(void);
// 有序停止当前 MQTT 运行连接。返回公共核心的停止进度结果，具体完成必须结合状态；不是立即断电接口。
int MQTT_Core_Stop(uint32_t now_ms);
// 尝试独占借用 MQTT 与 OTA 共用的工作区。返回借用结果；成功后调用者必须按约定归还，避免静态 RAM 工作区被并发覆盖。
int MQTT_Core_BorrowWorkspace(void);
// 归还先前借用的共享工作区。仅在借用者完成当前使用后调用，归还不表示 OTA 或 MQTT 事务已成功。
void MQTT_Core_ReturnWorkspace(void);
// 查询当前是否允许后台 AT 查询占用通道。在发布、接收或维护事务占用时由核心限制，避免打断共享解析器状态。
uint8_t MQTT_Core_CanRunBackgroundAt(void);
// 读取兼容层 MQTT 状态枚举。用于诊断和恢复调度，调用者不得自行改写适配器内部状态。
MQTT_CoreState_t MQTT_Core_GetState(void);
// 查询是否存在在途 MQTT 发布。用于串行化共享发送缓冲和业务回执，不能将非活动解释为上条消息一定成功。
uint8_t MQTT_Core_IsPublishActive(void);
// 取得已完成发布的递增成功序号。用于观察新发布证明，单条业务结果仍应使用 operation 关联回执。
uint32_t MQTT_Core_GetPubOkSeq(void);
// 按默认非 retain 方式提交 MQTT 消息。返回是否被核心接受，载荷应在约定生命周期内有效；接受不等于 Broker 或业务端完成处理。
uint8_t MQTT_Core_Publish(const char* topic, const char* payload, uint8_t qos);
// 提交显式 QoS 和 retain 的 MQTT 消息。复用公共核心发送接口，不额外创建私有重试队列；发布完成由后续状态决定。
uint8_t MQTT_Core_PublishEx(const char* topic, const char* payload, uint8_t qos, uint8_t retain);
// 提交消息并取得可追踪的发布操作号。仅在提交成功后按返回的 operation 查询回执，避免其他消息完成事件串扰当前业务。
uint8_t MQTT_Core_PublishExTracked(const char* topic, const char* payload,
                                  uint8_t qos, uint8_t retain, uint64_t* operation);
// 查询指定发布操作的回执枚举。区分在途、成功、失败或无效操作，不以任意成功序号替代准确操作关联。
ABoxMqttReceiptStatus MQTT_Core_PublishResult(uint64_t operation);
// 取消当前尚待完成的发布事务。取消会使对应业务等待失效，上层负责清理载荷和 operation 的关联状态。
void MQTT_Core_CancelPendingPublish(void);
// 查询 ICCID 缓存是否已有有效结果。仅反映缓存有效性，不作为网络就绪或设备注册成功的证明。
uint8_t MQTT_Core_IsIccidValid(void);
// 取得核心保存的 ICCID 文本。返回内部存储指针，读取前应检查有效标志，调用者不得修改或释放。
const char* MQTT_Core_GetIccid(void);
// 查询 ICCID 请求是否仍在排队或进行。用于后台查询节流，避免每轮重复提交相同 AT 请求。
uint8_t MQTT_Core_IsIccidPending(void);
// 查询核心是否正在收集 MQTT 接收载荷。用于共享 AT 通道避让，不应在该阶段直接注入不相关命令。
uint8_t MQTT_Core_IsRxCollecting(void);
// 复制 MQTT 接收路径诊断状态。输出累计错误和接收上下文，供内部查询，不改变接收事务。
void MQTT_Core_GetRxDiagnostics(MQTT_CoreRxDiagnostics_t* diagnostics);

// 返回公共 EC800 MQTT bridge 实例。供平台 OTA 适配器绑定共享传输，指针生命周期由本模块维护，不得释放或另起并行调度。
ABoxMqttEc800Bridge *MQTT_Core_Bridge(void);

// 读取 MQTT 进入就绪阶段的事件序号。用于识别新的连接周期，不把旧周期就绪事件作为新配置已验证的证明。
uint32_t MQTT_Core_GetReadySeq(void);

#endif
