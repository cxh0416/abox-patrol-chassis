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
    char client_id[64];
    char username[32];
    char password[64];
    char sub_topics[4][128];
    uint8_t sub_topic_count;
    char pub_topic[128];
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

void MQTT_Core_Init(const MQTT_CoreConfig_t* cfg, MQTT_CoreMsgCallback_t cb);
void MQTT_Core_SetAtCore(Ec800AtCore_t* at_core);
void MQTT_Core_Reset(void);
void MQTT_Core_Task(void);
void MQTT_Core_RequestReconnect(void);
void MQTT_Core_ForceReconnect(void);
void MQTT_Core_SetPaused(uint8_t paused);
void MQTT_Core_SetSecurityReady(uint8_t ready);
void MQTT_Core_SetNetworkChangedCallback(void (*callback)(void));
/* Register the existing shared RAM with OTA at startup. The App OTA callback
 * must first stop MQTT and acquire the workspace lease; it returns the lease
 * before resuming MQTT. */
uint8_t* MQTT_Core_OtaWorkspace(uint32_t* capacity);
void MQTT_Core_UpdateBroker(const char* host, uint16_t port, const char* username, const char* password);
/* Copies the normalized Platform runtime view. Product Flash layouts must be
 * mapped before this boundary. Security binding is completed by mqtt_runtime. */
uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig* config);
void MQTT_Core_RequestIccid(void);
void MQTT_Core_SetTopics(const char* const* sub_topics, uint8_t sub_topic_count, const char* pub_topic);
uint8_t MQTT_Core_SetClientId(const char* client_id);
uint8_t MQTT_Core_IsReady(void);
uint8_t MQTT_Core_IsConnected(void);
int MQTT_Core_Stop(uint32_t now_ms);
int MQTT_Core_BorrowWorkspace(void);
void MQTT_Core_ReturnWorkspace(void);
uint8_t MQTT_Core_CanRunBackgroundAt(void);
MQTT_CoreState_t MQTT_Core_GetState(void);
uint8_t MQTT_Core_IsPublishActive(void);
uint32_t MQTT_Core_GetPubOkSeq(void);
uint8_t MQTT_Core_Publish(const char* topic, const char* payload, uint8_t qos);
uint8_t MQTT_Core_PublishEx(const char* topic, const char* payload, uint8_t qos, uint8_t retain);
uint8_t MQTT_Core_PublishExTracked(const char* topic, const char* payload,
                                  uint8_t qos, uint8_t retain, uint64_t* operation);
ABoxMqttReceiptStatus MQTT_Core_PublishResult(uint64_t operation);
void MQTT_Core_CancelPendingPublish(void);
uint8_t MQTT_Core_IsIccidValid(void);
const char* MQTT_Core_GetIccid(void);
uint8_t MQTT_Core_IsIccidPending(void);
uint8_t MQTT_Core_IsRxCollecting(void);
void MQTT_Core_GetRxDiagnostics(MQTT_CoreRxDiagnostics_t* diagnostics);

ABoxMqttEc800Bridge *MQTT_Core_Bridge(void);

#endif
