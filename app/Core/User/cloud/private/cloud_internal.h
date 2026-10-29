#ifndef CLOUD_INTERNAL_H
#define CLOUD_INTERNAL_H

/* Cloud task owns this context. Only DMA receive callbacks touch ec800_rx.
 * Internal implementation details stay out of public service headers. */
#include <stdint.h>
#include "abox_ec800_at_legacy.h"
#include "abox_ec800_rx.h"
#include "abox_cloud_service.h"
#include "app_log.h"
#include "cloud_protocol.h"
#include "cloud_telemetry.h"

#define CLOUD_RX_DMA_SIZE (EC800_AT_RAW_CHUNK_MAX + 64U)
#define CLOUD_RX_RING_SIZE 4096U
#define CLOUD_PAYLOAD_SIZE 1536U
#define CLOUD_PAYLOAD_LIMIT 1535U
#define CLOUD_PUBLIC_PAYLOAD_MAX 1024U
#define CLOUD_PUBLIC_OBJECT_MAX 768U
#define CLOUD_TOPIC_SIZE 64U /* 11-byte prefix + 31-byte VID + 21-byte suffix + NUL */
#define CLOUD_REQUEST_ID_SIZE CLOUD_PROTOCOL_REQUEST_ID_SIZE
#define CLOUD_REBOOT_DELAY_MS 300U
/* Keep the diagnostic snapshot bounded so the shared TLS manager fits while
 * retaining the formal 4 KiB RAM guard. Transport payload limits are unchanged. */
#define CLOUD_LOG_SNAPSHOT_SIZE 1536U
#define CLOUD_ALERT_PAYLOAD_SIZE 512U
#define CLOUD_LOG_MAX_LINES 32U
#define CLOUD_LOG_LINES_PER_PACKET 6U

typedef enum {
    CLOUD_TX_NONE = 0,
    CLOUD_TX_MANIFEST,
    CLOUD_TX_HEARTBEAT,
    CLOUD_TX_STATUS_REPORT,
    CLOUD_TX_RESPONSE,
    CLOUD_TX_LOG_CHUNK,
    CLOUD_TX_ALERT,
} CloudTxKind_t;

typedef struct {
    char* buffer;
    uint16_t capacity;
    uint16_t length;
    uint8_t valid;
} JsonWriter_t;

typedef struct {
    uint8_t valid;
    uint8_t safety_ready;
    uint8_t fault_latched;
    uint8_t reversal_pending;
    uint8_t power_output_on;
    uint8_t stop_reason;
    int16_t target_rpm;
    int32_t target_steering_mdeg;
} ChassisStatusSnapshot_t;

typedef struct {
    Ec800AtCore_t at_core;
    uint8_t uart_dma[CLOUD_RX_DMA_SIZE];
    uint8_t rx_ring[CLOUD_RX_RING_SIZE];
    ABoxEc800Rx ec800_rx;

    char topic_heartbeat[64];
    char topic_manifest[64];
    char topic_status_report[64];
    char topic_config_cmd[CLOUD_TOPIC_SIZE];
    char topic_config_resp[CLOUD_TOPIC_SIZE];
    char topic_ota_cmd[CLOUD_TOPIC_SIZE];
    char topic_ota_resp[CLOUD_TOPIC_SIZE];
    char topic_log[CLOUD_TOPIC_SIZE];
    char topic_control_cmd[CLOUD_TOPIC_SIZE];
    char topic_control_resp[CLOUD_TOPIC_SIZE];
    ABoxCloudService cloud;
    char pending_sync_request_id[65];
    uint8_t external_tx_kind;

    uint8_t initialized;
    uint8_t config_ready;
    uint8_t mqtt_was_ready;
    uint8_t reset_reason;
    uint8_t ota_response_waiting;
    uint8_t ota_begin_pending;
    uint8_t tls_provision_started;
    char ota_request_id[CLOUD_REQUEST_ID_SIZE];
    uint32_t status_report_sequence;
    const char *status_report_request_id;
    CloudTelemetry telemetry;
    ChassisStatusSnapshot_t status_snapshot;
    uint32_t iccid_due;
    uint32_t ota_begin_due;

    char tx_payload[CLOUD_PAYLOAD_SIZE];

    uint8_t response_reboots;
    uint8_t response_ack_rollback;
    uint8_t mqtt_candidate_pending;
    uint8_t mqtt_persistent_active;
    char mqtt_trial_request_id[CLOUD_REQUEST_ID_SIZE];
    uint8_t mqtt_cancel_pending;
#if PATROL_CHASSIS_MQTT_LAB_TEST
    uint8_t modem_restart_pending;
#endif
    char response_topic[CLOUD_TOPIC_SIZE];
    char response_payload[CLOUD_PAYLOAD_SIZE];

    char last_request_id[CLOUD_REQUEST_ID_SIZE];
    char last_response_topic[CLOUD_TOPIC_SIZE];
    char last_response_payload[CLOUD_PAYLOAD_SIZE];

    char log_request_id[CLOUD_REQUEST_ID_SIZE];
    char log_snapshot[CLOUD_LOG_SNAPSHOT_SIZE];
    uint16_t log_line_offset[CLOUD_LOG_MAX_LINES];
    uint16_t log_line_length[CLOUD_LOG_MAX_LINES];
    uint8_t log_chunk_start[CLOUD_LOG_MAX_LINES];
    uint8_t log_chunk_count[CLOUD_LOG_MAX_LINES];
    uint8_t log_line_count;
    uint8_t log_chunk_total;
    uint8_t log_chunk_index;
    uint8_t log_active;

    uint8_t alert_pending;
    char alert_payload[CLOUD_ALERT_PAYLOAD_SIZE];
    uint8_t reboot_pending;
    uint32_t reboot_due;

} CloudContext;

extern CloudContext g_cloud;

// 将文本有界复制到指定缓冲区。处理空源和容量边界，确保可用空间内有终止符；不用于证明源字段通过业务验证。
void CloudMessages_CopyString(char* dst, uint16_t size, const char* src);
// 缓存最近已完成发送的请求响应。保存 requestId、Topic 和载荷以支持重复请求，缓存不取代动作摘要冲突检查。
void CloudMessages_CacheResponse(const char* request_id, const char* topic, const char* payload);
// 构造并排队一条内部管理响应。编码请求关联、结果码、说明及结果 JSON；reboot 仅登记发送完成后的重启意图，不在此立即复位。
uint8_t CloudMessages_ScheduleResponse(const char* topic, const char* request_id, uint16_t code, const char* message,
                                 const char* result_json, uint8_t reboot);
// 安排不带复杂结果对象的错误响应。复用统一响应构造与单响应队列，避免各命令自行拼接不一致结构。
void CloudMessages_ScheduleSimpleError(const char* topic, const char* request_id, uint16_t code, const char* message);
// 排队一条已经编码的完整响应载荷。检查缓冲区与当前排队状态，调用者负责载荷已满足对应协议格式。
uint8_t CloudMessages_ScheduleRawResponse(const char* topic, const char* payload);
// 构造公共同步请求对应的响应任务。通过统一公共协议字段和可信时间生成载荷，最终是否发送由 CloudTx 跟踪回执。
uint8_t CloudMessages_ScheduleSyncResponse(const char* request_id);
// 格式化内部设备信息结果对象。汇总固件、配置、Boot 与运行诊断，使用受限输出缓冲区，不触发硬件动作。
void CloudMessages_BuildInfoResult(char* output, uint16_t size);
// 构造公共 MQTT 心跳消息。使用产品身份及公共字段语义，不在公共接口中写入内部控制过程记录。
uint8_t CloudMessages_BuildHeartbeat(char* output, uint16_t size);
// 构造产品公共能力清单消息。从正式产品注册表声明能力及版本语义，构造成功不意味着所有能力已通过实车测试。
uint8_t CloudMessages_BuildManifest(char* output, uint16_t size);
// 保存一次已提交状态报告所对应的比较快照。后续事件检测据此识别目标、使能与故障变化，快照属于发送调度状态而非不可变现场证据。
void CloudMessages_RememberStatusSnapshot(uint32_t now);
// 比较当前运行状态与上次报告快照。返回需要唤醒状态上报的原因文本，未发生相关变化时返回空指针。
const char* CloudMessages_StatusChangeReason(uint32_t now);
// 构造公共底盘状态报告。在容量内编码当前状态和触发原因；请求 ID 由当前调度上下文提供，不在构造时判定真实运动成功。
uint8_t CloudMessages_BuildStatusReport(char* output, uint16_t size, const char* report_reason);
// 截取一次有界日志快照并初始化分片。关联请求 ID，限制总行数和存储容量，后续分片只读取该次快照而非持续移动的日志环。
void CloudMessages_PrepareLogSnapshot(const char* request_id);
// 构造当前日志分片消息。按转义后容量和每片行数限制编码，分片索引在发送完成后再推进，避免失败时跳过数据。
uint8_t CloudMessages_BuildLogChunk(char* output, uint16_t size);
// 将一条指定等级日志编码为告警载荷。执行 JSON 转义和容量检查，是否发布由发送调度决定，不阻塞运动任务等待网络。
uint8_t CloudMessages_BuildAlert(char* output, uint16_t size, const char* line, AppLogLevel_t level);
// 构造中位标定管理响应结果。汇总当前业务状态与诊断，不能把单次 ACK 或格式化成功解释为标定落盘成功。
void CloudMessages_BuildCalibrationResult(char* output, uint16_t size);
// 按 Topic 分发收到的 MQTT 消息。优先识别时间同步、公共控制和内部维护入口，未知 Topic 不执行动作；解析结果仍须通过各业务门禁。
void CloudCommands_OnMessage(const char* topic, const char* payload);
// 执行一次云消息与状态事件发送调度。比较状态变化、轮询公共发送器，空闲时再安排 ICCID 查询；消息完成以跟踪回执为准。
void CloudTx_Service(void);
// 清除网络变更前的云发送在途关联。通知公共云服务，重置外部消息类型及候选取消标记，避免旧回执完成新连接的动作。
void CloudTx_NetworkChanged(void);
// 向云服务调度提供当前毫秒单调时钟。使用 HAL_GetTick 端口，不将返回值作为 UTC 或请求有效期绝对时间。
uint32_t CloudPort_Now(void);
#endif
