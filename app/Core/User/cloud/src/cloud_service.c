#include "abox_app_service.h"
#include "cloud_service.h"
#include "time_sync.h"
#include "cloud_internal.h"
#include <stdio.h>
#include <string.h>
#include "app_log.h"
#include "ec_power.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "mqtt_config_adapter.h"
#include "abox_mqtt_v4.h"
#include "ota_service.h"
#include "usart.h"
#include "patrol_config.h"
#include "patrol_enrollment.h"
#include "remote_service.h"
#ifdef ABOX_PSK_STATION
#include "abox_psk_station.h"
#include "mqtt_trial_persistent.h"
#endif

CloudContext g_cloud;
static uint32_t g_enroll_provision_due;
static ABoxEc800Recovery g_modem_recovery;
static void mqtt_network_changed(void)
{
    CloudTx_NetworkChanged();
    RemoteService_RevokeForNetworkChange();
}

_Static_assert(64U >= sizeof(((PatrolRemoteConfigV5_t*)0)->vid) + 21U,
               "public topic storage must cover maximum Config V5 VID");

static uint8_t build_public_topics(const char* vid)
{
    return ABoxMqttV4_BuildTopic(g_cloud.topic_heartbeat, sizeof(g_cloud.topic_heartbeat),
                                 vid, ABOX_MQTT_V4_TOPIC_HEARTBEAT) &&
           ABoxMqttV4_BuildTopic(g_cloud.topic_manifest, sizeof(g_cloud.topic_manifest),
                                 vid, ABOX_MQTT_V4_TOPIC_MANIFEST) &&
           ABoxMqttV4_BuildTopic(g_cloud.topic_status_report, sizeof(g_cloud.topic_status_report),
                                 vid, ABOX_MQTT_V4_TOPIC_REPORT) &&
           ABoxMqttV4_BuildTopic(g_cloud.topic_control_cmd, sizeof(g_cloud.topic_control_cmd),
                                 vid, ABOX_MQTT_V4_TOPIC_REQUEST) &&
           ABoxMqttV4_BuildTopic(g_cloud.topic_control_resp, sizeof(g_cloud.topic_control_resp),
                                 vid, ABOX_MQTT_V4_TOPIC_RESPONSE);
}

uint8_t CloudService_NeedsEnrollment(void)
{
    const PatrolRemoteConfigV5_t *cfg = PatrolConfig_Get();
    ABoxMqttTrialState state = MqttTrialManager_State();
    if (state >= ABOX_MQTT_TRIAL_WAIT_ACCEPT && state <= ABOX_MQTT_TRIAL_RESTORING) return 0;
    return cfg != 0 && (!PatrolConfig_IsCloudReady(cfg) ||
           (cfg->reserved0 != 1U && strcmp(cfg->vid, PATROL_CONFIG_DEFAULT_VID) == 0));
}

uint32_t CloudPort_Now(void) { return HAL_GetTick(); }

uint8_t CloudService_ApplyMqttIdentity(const char* vid)
{
    const char* subscriptions[4];
    if (!PatrolConfig_IsVidValid(vid)) return 0U;
    if (!MQTT_Core_SetClientId(vid)) return 0U;
    if (!build_public_topics(vid)) return 0U;
    (void)snprintf(g_cloud.topic_config_cmd, sizeof(g_cloud.topic_config_cmd), "/zxwl/abox/%s/internal/config", vid);
    (void)snprintf(g_cloud.topic_config_resp, sizeof(g_cloud.topic_config_resp), "/zxwl/abox/%s/internal/config/resp", vid);
    (void)snprintf(g_cloud.topic_ota_cmd, sizeof(g_cloud.topic_ota_cmd), "/zxwl/abox/%s/internal/ota", vid);
    (void)snprintf(g_cloud.topic_ota_resp, sizeof(g_cloud.topic_ota_resp), "/zxwl/abox/%s/internal/ota/resp", vid);
    (void)snprintf(g_cloud.topic_log, sizeof(g_cloud.topic_log), "/zxwl/abox/%s/internal/log", vid);
    subscriptions[0] = g_cloud.topic_config_cmd;
    subscriptions[1] = g_cloud.topic_ota_cmd;
    subscriptions[2] = g_cloud.topic_control_cmd;
    TimeSync_Init(vid);
    subscriptions[3] = TimeSync_ResponseTopic();
    MQTT_Core_SetTopics(subscriptions, 4U, g_cloud.topic_heartbeat);
    return 1U;
}

static uint8_t read_reset_reason(void)
{
    uint8_t reason = 0U;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) reason = 3U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) reason = 4U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) reason = 5U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) reason = 6U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) reason = 2U;
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) reason = 1U;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return reason;
}

static void uart_start_rx(void)
{
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_cloud.uart_dma, sizeof(g_cloud.uart_dma));
}

static uint32_t ec800_tick(void *context)
{
    (void)context;
    return HAL_GetTick();
}

static int ec800_write(void *context, const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms)
{
    return HAL_UART_Transmit((UART_HandleTypeDef *)context, (uint8_t *)data,
                             length, timeout_ms) == HAL_OK;
}


static ABoxAppService g_app_service;
static void app_cloud(void *context, uint32_t now);
static int app_before_cloud(void *, uint32_t);
static int app_can_connect(void *);
static void app_runtime(void *, uint32_t);
static int app_runtime_ready(void *);
static void app_transport(void *, uint32_t);
static int app_transport_ready(void *);
static int app_transport_error(void *);
static int app_maintenance_busy(void *);
static void app_event(void *, ABoxAppServiceEvent);
static void app_restarted(void *, uint32_t);
static void app_ready(void *, int, uint32_t);

static void app_before_enrollment(void *context, uint32_t now);
static void app_enrollment(void *context, uint32_t now);
static void app_restart_rx(void *context);
static const ABoxAppServicePort app_port = {
    .restart_rx = app_restart_rx,
    .before_enrollment = app_before_enrollment,
    .enrollment = app_enrollment,
    .cloud = app_cloud, .before_cloud = app_before_cloud, .can_connect = app_can_connect,
    .runtime = app_runtime, .runtime_ready = app_runtime_ready,
    .transport = app_transport, .transport_ready = app_transport_ready, .ready = app_ready,
    .transport_error = app_transport_error, .maintenance_busy = app_maintenance_busy,
    .recovery = &g_modem_recovery, .event = app_event, .restarted = app_restarted
};

void CloudService_Init(void)
{
    memset(&g_cloud.telemetry, 0, sizeof(g_cloud.telemetry));
    MQTT_CoreConfig_t mqtt;
    ABoxMqttConfig runtime;
    const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
    memset(&mqtt, 0, sizeof(mqtt));
    (void)ABoxEc800Rx_Init(&g_cloud.ec800_rx, g_cloud.uart_dma, sizeof(g_cloud.uart_dma),
                           g_cloud.rx_ring, sizeof(g_cloud.rx_ring));
    const ABoxCloudServiceOptions cloud_options = { CLOUD_IDLE_HEARTBEAT_MS, CLOUD_IDLE_STATUS_MS, 0U };
    (void)ABoxCloudService_Init(&g_cloud.cloud, &cloud_options, HAL_GetTick());
    (void)ABoxCloudService_SetSyncStorage(&g_cloud.cloud, g_cloud.pending_sync_request_id,
                                        sizeof(g_cloud.pending_sync_request_id));
    g_cloud.cloud.response_pending = 0U;
    g_cloud.log_active = 0U;
    g_cloud.alert_pending = 0U;
    g_cloud.reboot_pending = 0U;
    g_cloud.ota_begin_pending = 0U;
    g_cloud.last_request_id[0] = '\0';
    g_cloud.last_response_payload[0] = '\0';
    g_cloud.mqtt_persistent_active = 0U;
    g_cloud.mqtt_candidate_pending = 0U;
#if PATROL_CHASSIS_MQTT_LAB_TEST
    g_cloud.modem_restart_pending = 0U;
#endif
    g_cloud.reset_reason = read_reset_reason();
    g_enroll_provision_due = HAL_GetTick();
    ABoxEc800Recovery_Init(&g_modem_recovery, 120000U);
    g_modem_recovery.waiting = 1U;
    g_modem_recovery.next_restart_ms = HAL_GetTick() + 60000U;
    g_cloud.config_ready = !CloudService_NeedsEnrollment();

    {
        const ABoxEc800AtPort at_port = {&huart1, ec800_tick, ec800_write, 0};
        (void)EC800_AT_Init(&g_cloud.at_core, &at_port);
    }
    OtaService_Init(&g_cloud.at_core);
    memset(&runtime, 0, sizeof(runtime));
    if (cfg != 0)
    {
        CloudMessages_CopyString(mqtt.server_host, sizeof(mqtt.server_host), cfg->mqtt_host);
        mqtt.server_port = cfg->mqtt_port;
        CloudMessages_CopyString(mqtt.username, sizeof(mqtt.username), cfg->mqtt_username);
        CloudMessages_CopyString(mqtt.password, sizeof(mqtt.password), cfg->mqtt_password);
        CloudMessages_CopyString(mqtt.client_id, sizeof(mqtt.client_id), cfg->vid);
        if (!PatrolMqttConfig_FromV5(cfg, &runtime)) g_cloud.config_ready = 0U;
        if (!build_public_topics(cfg->vid)) g_cloud.config_ready = 0U;
        (void)snprintf(g_cloud.topic_config_cmd, sizeof(g_cloud.topic_config_cmd), "/zxwl/abox/%s/internal/config", cfg->vid);
        (void)snprintf(g_cloud.topic_config_resp, sizeof(g_cloud.topic_config_resp), "/zxwl/abox/%s/internal/config/resp", cfg->vid);
        (void)snprintf(g_cloud.topic_ota_cmd, sizeof(g_cloud.topic_ota_cmd), "/zxwl/abox/%s/internal/ota", cfg->vid);
        (void)snprintf(g_cloud.topic_ota_resp, sizeof(g_cloud.topic_ota_resp), "/zxwl/abox/%s/internal/ota/resp", cfg->vid);
        (void)snprintf(g_cloud.topic_log, sizeof(g_cloud.topic_log), "/zxwl/abox/%s/internal/log", cfg->vid);
        CloudMessages_CopyString(mqtt.sub_topics[0], sizeof(mqtt.sub_topics[0]), g_cloud.topic_config_cmd);
        CloudMessages_CopyString(mqtt.sub_topics[1], sizeof(mqtt.sub_topics[1]), g_cloud.topic_ota_cmd);
        CloudMessages_CopyString(mqtt.sub_topics[2], sizeof(mqtt.sub_topics[2]), g_cloud.topic_control_cmd);
        TimeSync_Init(cfg->vid);
        CloudMessages_CopyString(mqtt.sub_topics[3], sizeof(mqtt.sub_topics[3]), TimeSync_ResponseTopic());
        mqtt.sub_topic_count = 4U;
        CloudMessages_CopyString(mqtt.pub_topic, sizeof(mqtt.pub_topic), g_cloud.topic_heartbeat);
    }
    MQTT_Core_Init(&mqtt, CloudCommands_OnMessage);
    MQTT_Core_SetNetworkChangedCallback(mqtt_network_changed);
    MQTT_Core_SetAtCore(&g_cloud.at_core);
    if (!MqttRuntime_Init(&g_cloud.at_core, g_cloud.config_ready ? &runtime : 0, HAL_GetTick())) {
        g_cloud.config_ready = 0U;
        APP_LOGE("CLOUD", "invalid MQTT V5 runtime configuration");
    }
    MqttTrialManager_Init();
    PatrolEnrollment_Init(&g_cloud.at_core);

    EC_Power_Init();
    EC_Power_StartOnSequence();
    uart_start_rx();
    g_cloud.cloud.heartbeat_due = HAL_GetTick();
    g_cloud.cloud.status_due = HAL_GetTick();
    g_cloud.status_report_sequence = 1U;
    memset(&g_cloud.status_snapshot, 0, sizeof(g_cloud.status_snapshot));
    g_cloud.iccid_due = HAL_GetTick() + 1000U;
    (void)ABoxAppService_Init(&g_app_service, &g_cloud.at_core, &g_cloud.ec800_rx, &app_port);
#ifdef ABOX_PSK_STATION
    ABoxPskStation_SetInputBuffer(g_cloud.last_response_payload, sizeof(g_cloud.last_response_payload));
#endif
    g_cloud.initialized = 1U;
    if (g_cloud.config_ready) APP_LOGI("CLOUD", "configured vid=%s broker=%s:%u", cfg->vid, cfg->mqtt_host, cfg->mqtt_port);
    else APP_LOGW("CLOUD", "awaiting HTTPS enrollment");
}

#ifdef ABOX_PSK_STATION
static int stage_station_psk(const ABoxPskStationInput *input)
{
    PatrolRemoteConfigV5_t next = *PatrolConfig_Get();
    ABoxMqttConfig runtime;
    const ABoxTlsCredentials *credentials = &input->credentials;
    if (!CloudService_NeedsEnrollment() ||
        credentials->generation <= PatrolConfig_MqttCredentials()->generation) return 0;
    if (input->host[0]) strcpy(next.mqtt_host, input->host);
    if (input->username[0]) strcpy(next.mqtt_username, input->username);
    if (input->password[0]) strcpy(next.mqtt_password, input->password);
    next.mqtt_port = input->port;
    next.mqtt_tls_enabled = 1;
    next.mqtt_tls_profile_id = PATROL_CONFIG_TLS_PROFILE_DEFAULT;
    if (!PatrolMqttConfig_FromV5(&next, &runtime)) return 0;
    runtime.tls_credentials = *credentials;
    if (!MqttTrialManager_StartEnrollment(&runtime, next.vid, HAL_GetTick())) return 0;
    g_cloud.config_ready = 1;
    MqttTrialManager_Accepted(HAL_GetTick());
    return 1;
}
#endif

void CloudService_Task(void)
{
    if (!g_cloud.initialized) return;
    ABoxAppService_Poll(&g_app_service, HAL_GetTick());
#ifdef ABOX_PSK_STATION
    if (!CloudService_NeedsEnrollment()) ABoxPskStation_SetInputBuffer(NULL, 0);
    else if (!ABoxPskStation_Input)
        ABoxPskStation_SetInputBuffer(g_cloud.last_response_payload, sizeof(g_cloud.last_response_payload));
    ABoxPskStation_Poll(stage_station_psk);
#endif
}

static void app_restart_rx(void *context)
{
    (void)context;
    (void)HAL_UART_DMAStop(&huart1);
    ABoxEc800Rx_AfterRestart(&g_cloud.ec800_rx);
    uart_start_rx();
}
static void app_before_enrollment(void *context, uint32_t now)
{
    (void)context; (void)now;
    if (CloudService_NeedsEnrollment() && EC_Power_IsOnDone() &&
        !OtaService_IsProvisioned() && !OtaService_IsBusy() &&
        (int32_t)(HAL_GetTick() - g_enroll_provision_due) >= 0) {
        if (OtaService_BeginProvisioning())
            g_enroll_provision_due = HAL_GetTick() + 60000U;
    }
}
static void app_enrollment(void *context, uint32_t now)
{
    (void)context; (void)now;
    PatrolEnrollment_Task();
}
static int app_before_cloud(void *context, uint32_t now)
{
    (void)context; (void)now;
    MqttTrialManager_Poll(HAL_GetTick());
    if (g_cloud.ota_response_waiting && OtaService_TakeInstallReady())
    {
        char result[128];
        (void)snprintf(result, sizeof(result), "{\"fwVersion\":\"%s\",\"otaPhase\":\"INSTALL_PENDING\"}", OtaService_TargetVersion());
        if (CloudMessages_ScheduleResponse(g_cloud.topic_ota_resp, g_cloud.ota_request_id, 200U, "INSTALL_PENDING", result, 1U))
            g_cloud.ota_response_waiting = 0U;
    }
    if (g_cloud.ota_response_waiting)
    {
        uint32_t ota_error;
        if (OtaService_TakeFailure(&ota_error))
        {
            char result[64];
            (void)snprintf(result, sizeof(result), "{\"errorCode\":%lu}", (unsigned long)ota_error);
            if (CloudMessages_ScheduleResponse(g_cloud.topic_ota_resp, g_cloud.ota_request_id, 500U, "OTA_FAILED", result, 0U))
                g_cloud.ota_response_waiting = 0U;
        }
    }
    if (!g_cloud.config_ready || !EC_Power_IsOnDone()) return 0;
    if (PatrolConfig_Get() != 0 && PatrolConfig_Get()->mqtt_tls_enabled &&
        PatrolConfig_MqttCredentials()->mode == ABOX_TLS_AUTH_CA && !OtaService_IsProvisioned() &&
        !g_cloud.tls_provision_started)
    {
        if (OtaService_BeginProvisioning()) g_cloud.tls_provision_started = 1U;
    }
    return 1;
}
static int app_can_connect(void *context) { (void)context; return g_cloud.config_ready && EC_Power_IsOnDone(); }
static void app_runtime(void *context, uint32_t now) { (void)context; MqttRuntime_Task(now); }
static int app_runtime_ready(void *context) { (void)context; return MqttRuntime_IsReady(); }
static void app_transport(void *context, uint32_t now) { (void)context; (void)now; MQTT_Core_Task(); }
static int app_transport_ready(void *context) { (void)context; return MQTT_Core_IsReady(); }
static int app_transport_error(void *context) { (void)context; return MQTT_Core_GetState() == MQTT_CORE_ST_ERROR; }
static int app_maintenance_busy(void *context)
{
    ABoxMqttTrialState state = MqttTrialManager_State(); (void)context;
    return state != ABOX_MQTT_TRIAL_IDLE && state != ABOX_MQTT_TRIAL_COMMITTED && state != ABOX_MQTT_TRIAL_FAILED;
}
static void app_event(void *context, ABoxAppServiceEvent event)
{
    (void)context;
    if (event == ABOX_APP_NETWORK_CHANGED) mqtt_network_changed();
}
static void app_restarted(void *context, uint32_t now)
{
    (void)context; (void)now;
    APP_LOGW("CLOUD", "MQTT transport blocked; restart EC800");
}
static void app_ready(void *context, int ready, uint32_t now)
{
    (void)context; (void)now;
    ABoxCloudService_SetReady(&g_cloud.cloud, ready, HAL_GetTick());
    if (ready && !g_cloud.mqtt_was_ready)
    {
        g_cloud.cloud.heartbeat_due = HAL_GetTick();
        g_cloud.cloud.status_due = HAL_GetTick();
        g_cloud.ota_begin_due = HAL_GetTick() + 1000U;
        g_cloud.ota_begin_pending = 1U;
        APP_LOGI("CLOUD", "mqtt ready");
    }
    else if (!ready && g_cloud.mqtt_was_ready) {
        ABoxCloudService_SetReady(&g_cloud.cloud, 0, HAL_GetTick());
        APP_LOGW("CLOUD", "mqtt disconnected");
    }
    g_cloud.mqtt_was_ready = ready;
 }
static void app_cloud(void *context, uint32_t now)
{
    uint8_t ready = MQTT_Core_IsReady();
    (void)context; (void)now;
    if (g_cloud.mqtt_persistent_active && !g_cloud.mqtt_candidate_pending &&
        !g_cloud.cloud.response_pending) {
        ABoxMqttTrialState trial_state = MqttTrialManager_State();
        if (trial_state == ABOX_MQTT_TRIAL_COMMITTED || trial_state == ABOX_MQTT_TRIAL_FAILED) {
            const uint8_t saved = trial_state == ABOX_MQTT_TRIAL_COMMITTED;
            if (CloudMessages_ScheduleResponse(g_cloud.topic_config_resp, g_cloud.mqtt_trial_request_id,
                    saved ? 200U : 500U, saved ? "MQTT_CONFIG_SAVED" : "MQTT_CANDIDATE_RESTORED",
                    saved ? "{\"committed\":true}" : "{\"committed\":false,\"restored\":true}", 0U))
                g_cloud.mqtt_persistent_active = 0U;
        }
    }
    CloudTx_Service();
#if PATROL_CHASSIS_MQTT_LAB_TEST
    if (g_cloud.modem_restart_pending && !g_cloud.cloud.response_pending &&
        g_cloud.cloud.in_flight != ABOX_CLOUD_JOB_RESPONSE) {
        g_cloud.modem_restart_pending = 0U;
        g_modem_recovery.next_restart_ms = HAL_GetTick() + 120000U;
        CloudTx_NetworkChanged();
        RemoteService_RevokeForNetworkChange();
        EC_Power_StartRestartSequence();
        g_cloud.mqtt_was_ready = 0U;
        return;
    }
#endif
    if (ready && g_cloud.ota_begin_pending && (int32_t)(HAL_GetTick() - g_cloud.ota_begin_due) >= 0 &&
        !EC800_AT_IsBusy(&g_cloud.at_core))
    {
        if (OtaService_BeginProvisioning()) g_cloud.ota_begin_pending = 0U;
    }
    if (g_cloud.reboot_pending && (int32_t)(HAL_GetTick() - g_cloud.reboot_due) >= 0)
    {
        __disable_irq();
        NVIC_SystemReset();
    }
}

void CloudService_OnUartRxEvent(UART_HandleTypeDef* huart, uint16_t size)
{
    if ((huart == 0) || (huart->Instance != USART1)) return;
    (void)ABoxEc800Rx_OnDmaEvent(&g_cloud.ec800_rx, size);
}

void CloudService_OnUartError(UART_HandleTypeDef* huart)
{
    if ((huart == 0) || (huart->Instance != USART1)) return;
    ABoxEc800Rx_RequestRestart(&g_cloud.ec800_rx);
}
