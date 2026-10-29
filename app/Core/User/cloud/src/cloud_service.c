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
// 响应网络身份或配置切换事件。同时清理云发送关联状态并撤销底盘远控授权，避免旧连接下的目标继续有效。
static void mqtt_network_changed(void)
{
    CloudTx_NetworkChanged();
    RemoteService_RevokeForNetworkChange();
}

_Static_assert(64U >= sizeof(((PatrolRemoteConfigV5_t*)0)->vid) + 21U,
               "public topic storage must cover maximum Config V5 VID");

// 根据有效 VID 构造全部公共 MQTT Topic。复用平台 V4 Topic 构造器，任何一个容量或身份校验失败都拒绝整组配置。
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

// 判断当前配置是否仍需首次身份与凭据注册。考虑试连中的保护阶段及默认 VID 标记，避免在已进行的维护流程中重复注册。
uint8_t CloudService_NeedsEnrollment(void)
{
    const PatrolRemoteConfigV5_t *cfg = PatrolConfig_Get();
    ABoxMqttTrialState state = MqttTrialManager_State();
    if (state >= ABOX_MQTT_TRIAL_WAIT_ACCEPT && state <= ABOX_MQTT_TRIAL_RESTORING) return 0;
    return cfg != 0 && (!PatrolConfig_IsCloudReady(cfg) ||
           (cfg->reserved0 != 1U && strcmp(cfg->vid, PATROL_CONFIG_DEFAULT_VID) == 0));
}

// 向云服务调度提供当前毫秒单调时钟。使用 HAL_GetTick 端口，不将返回值作为 UTC 或请求有效期绝对时间。
uint32_t CloudPort_Now(void) { return HAL_GetTick(); }

// 应用 VID 对应的 Client ID、Topic 与时间同步订阅。先验证 VID 并逐项构造公共和内部 Topic，再更新订阅集合；此操作不直接重建控制会话。
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

// 读取并归类本次 MCU 复位来源。按看门狗、软件、电源和引脚标志优先级编码后清除 RCC 复位标志，供启动报告使用。
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

// 启动 EC800 所用 USART1 的空闲线 DMA 接收。使用固定云接收缓冲区，返回 HAL 是否接受启动请求，不在此解析 AT 或 MQTT 消息。
static int uart_start_rx(void)
{
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_cloud.uart_dma, sizeof(g_cloud.uart_dma)) == HAL_OK;
}

// 为 EC800 AT 核心提供毫秒时钟回调。上下文在本适配器中未使用，返回 HAL tick 作为超时计算基准。
static uint32_t ec800_tick(void *context)
{
    (void)context;
    return HAL_GetTick();
}

// 通过绑定的 UART 发送 EC800 AT 字节。context 为 UART 句柄，使用给定毫秒超时；返回 HAL 发送结果，不能据此认定模块已执行命令。
static int ec800_write(void *context, const uint8_t *data, uint16_t length,
                       uint32_t timeout_ms)
{
    return HAL_UART_Transmit((UART_HandleTypeDef *)context, (uint8_t *)data,
                             length, timeout_ms) == HAL_OK;
}


static ABoxAppService g_app_service;
// 处理云业务发送、维护结果及延后重启。维护响应按试连结果安排，发送调度在此轮询；只有既有重启条件和期限成立时才执行系统复位。
static void app_cloud(void *context, uint32_t now);
// 在云连接前处理试连与 OTA 的阶段结果。安排安装就绪或失败响应并检查配置、模块电源与 TLS 预置条件；返回能否继续云服务阶段。
static int app_before_cloud(void *, uint32_t);
// 查询配置与模块上电是否允许开始连接。仅表示启动连接的前提，不表示已鉴权、订阅或能发布。
static int app_can_connect(void *);
// 推进 MQTT/TLS 运行时管理器。将调度提供的单调时间传入运行时，网络切换安全撤权由绑定回调保证。
static void app_runtime(void *, uint32_t);
// 查询 MQTT/TLS 运行时是否进入就绪状态。结果用于公共 App 服务阶段切换，不代替传输层逐次发布回执。
static int app_runtime_ready(void *);
// 推进 MQTT 传输核心一次。由统一 App 调度安排时序，避免多个任务同时推进共享 AT 事务。
static void app_transport(void *, uint32_t);
// 查询 MQTT 核心是否已连接且满足就绪条件。返回传输就绪标志，不表示当前业务响应已发送完成。
static int app_transport_ready(void *);
// 查询传输错误或运行时模块恢复请求。合并 MQTT 错误状态与 TLS 运行时恢复需求，交给统一恢复调度处理。
static int app_transport_error(void *);
// 判断 MQTT 维护流程是否禁止自动模块恢复。试连、恢复和阻塞阶段按状态限制自动重启，避免覆盖正在进行的持久化或回退事务。
static int app_maintenance_busy(void *);
// 处理公共 App 服务向产品发出的事件。网络变更事件触发发送状态清理及运动会话撤销，其他事件保持现有处理方式。
static void app_event(void *, ABoxAppServiceEvent);
// 记录 EC800 恢复重启事件。只写诊断日志，实际电源序列由公共恢复服务执行。
static void app_restarted(void *, uint32_t);
// 同步云就绪状态并安排首次上报与预置。上升沿唤醒心跳、状态报告及延后 OTA 文件预置，下降沿撤销云就绪；不自动恢复运动授权。
static void app_ready(void *, int, uint32_t);

// 在身份注册前安排必要的 OTA/TLS 文件预置。约束模块供电完成、OTA 空闲和节流期限，避免与注册或升级共享 AT 通道产生冲突。
static void app_before_enrollment(void *context, uint32_t now);
// 推进身份注册服务一次。作为公共 App 服务回调调用产品注册适配器，时间与上下文未另行修改。
static void app_enrollment(void *context, uint32_t now);
// 安全重启 EC800 DMA 接收路径。先停止 DMA、重置接收器状态再重启，任一步失败返回零，由上层安排恢复。
static int app_restart_rx(void *context);
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

// 初始化 EC800、MQTT、注册、OTA 与云调度适配器。绑定固定接收存储和平台服务，加载本车配置并建立安全默认状态；关键初始化失败时保持通信服务未就绪。
void CloudService_Init(void)
{
    g_cloud.initialized = 0U;
    memset(&g_cloud.telemetry, 0, sizeof(g_cloud.telemetry));
    MQTT_CoreConfig_t mqtt;
    ABoxMqttConfig runtime;
    const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
    memset(&mqtt, 0, sizeof(mqtt));
    if (!ABoxEc800Rx_Init(&g_cloud.ec800_rx, g_cloud.uart_dma, sizeof(g_cloud.uart_dma),
                           g_cloud.rx_ring, sizeof(g_cloud.rx_ring))) {
        APP_LOGE("CLOUD", "RX initialization failed; communication disabled");
        return;
    }
    const ABoxCloudServiceOptions cloud_options = { CLOUD_IDLE_HEARTBEAT_MS, CLOUD_IDLE_STATUS_MS, 0U };
    if (!ABoxCloudService_Init(&g_cloud.cloud, &cloud_options, HAL_GetTick())) {
        APP_LOGE("CLOUD", "cloud initialization failed; communication disabled");
        return;
    }
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
        if (!EC800_AT_Init(&g_cloud.at_core, &at_port)) {
            APP_LOGE("CLOUD", "AT initialization failed; communication disabled");
            return;
        }
    }
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
    MQTT_Core_SetModemResetCallback(PatrolEnrollment_AfterModemReset);
    MQTT_Core_SetAtCore(&g_cloud.at_core);
    if (!MQTT_Core_Bridge()->initialized) {
        APP_LOGE("CLOUD", "MQTT bridge initialization failed; communication disabled");
        return;
    }
    OtaService_Init(&g_cloud.at_core);
    if (!MqttRuntime_Init(&g_cloud.at_core, g_cloud.config_ready ? &runtime : 0, HAL_GetTick())) {
        g_cloud.config_ready = 0U;
        APP_LOGE("CLOUD", "invalid MQTT V5 runtime configuration");
        return;
    }
    MqttTrialManager_Init();
    if (!PatrolEnrollment_Init(&g_cloud.at_core))
        APP_LOGE("ENROLL", "initialization failed; enrollment unavailable");

    EC_Power_Init();
    EC_Power_StartOnSequence();
    g_cloud.cloud.heartbeat_due = HAL_GetTick();
    g_cloud.cloud.status_due = HAL_GetTick();
    g_cloud.status_report_sequence = 1U;
    memset(&g_cloud.status_snapshot, 0, sizeof(g_cloud.status_snapshot));
    g_cloud.iccid_due = HAL_GetTick() + 1000U;
    if (!ABoxAppService_Init(&g_app_service, &g_cloud.at_core, &g_cloud.ec800_rx, &app_port)) {
        APP_LOGE("CLOUD", "App service initialization failed; communication disabled");
        return;
    }
#ifdef ABOX_PSK_STATION
    ABoxPskStation_SetInputBuffer(g_cloud.last_response_payload, sizeof(g_cloud.last_response_payload));
#endif
    g_cloud.initialized = 1U;
    if (g_cloud.config_ready) APP_LOGI("CLOUD", "configured vid=%s broker=%s:%u", cfg->vid, cfg->mqtt_host, cfg->mqtt_port);
    else APP_LOGW("CLOUD", "awaiting HTTPS enrollment");
}

#ifdef ABOX_PSK_STATION
// 接收受控工位注入的下一代 PSK 候选配置。只在需要注册且凭据 generation 更新时允许，将持久化提交交给试连验证，失败不直接覆盖稳定凭据。
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

// 轮询公共 App 服务及可选工位注册流程。仅在初始化成功后执行，由云任务持有服务上下文；每轮处理有限状态，不在运动任务内阻塞等待网络。
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

// 安全重启 EC800 DMA 接收路径。先停止 DMA、重置接收器状态再重启，任一步失败返回零，由上层安排恢复。
static int app_restart_rx(void *context)
{
    (void)context;
    if (HAL_UART_DMAStop(&huart1) != HAL_OK) {
        APP_LOGE("UART", "RX stop failed");
        return 0;
    }
    ABoxEc800Rx_AfterRestart(&g_cloud.ec800_rx);
    if (!uart_start_rx()) {
        APP_LOGE("UART", "RX start failed");
        return 0;
    }
    return 1;
}
// 在身份注册前安排必要的 OTA/TLS 文件预置。约束模块供电完成、OTA 空闲和节流期限，避免与注册或升级共享 AT 通道产生冲突。
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
// 推进身份注册服务一次。作为公共 App 服务回调调用产品注册适配器，时间与上下文未另行修改。
static void app_enrollment(void *context, uint32_t now)
{
    (void)context; (void)now;
    PatrolEnrollment_Task();
}
// 在云连接前处理试连与 OTA 的阶段结果。安排安装就绪或失败响应并检查配置、模块电源与 TLS 预置条件；返回能否继续云服务阶段。
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
// 查询配置与模块上电是否允许开始连接。仅表示启动连接的前提，不表示已鉴权、订阅或能发布。
static int app_can_connect(void *context) { (void)context; return g_cloud.config_ready && EC_Power_IsOnDone(); }
// 推进 MQTT/TLS 运行时管理器。将调度提供的单调时间传入运行时，网络切换安全撤权由绑定回调保证。
static void app_runtime(void *context, uint32_t now) { (void)context; MqttRuntime_Task(now); }
// 查询 MQTT/TLS 运行时是否进入就绪状态。结果用于公共 App 服务阶段切换，不代替传输层逐次发布回执。
static int app_runtime_ready(void *context) { (void)context; return MqttRuntime_IsReady(); }
// 推进 MQTT 传输核心一次。由统一 App 调度安排时序，避免多个任务同时推进共享 AT 事务。
static void app_transport(void *context, uint32_t now) { (void)context; (void)now; MQTT_Core_Task(); }
// 查询 MQTT 核心是否已连接且满足就绪条件。返回传输就绪标志，不表示当前业务响应已发送完成。
static int app_transport_ready(void *context) { (void)context; return MQTT_Core_IsReady(); }
// 查询传输错误或运行时模块恢复请求。合并 MQTT 错误状态与 TLS 运行时恢复需求，交给统一恢复调度处理。
static int app_transport_error(void *context) { (void)context; return MQTT_Core_GetState() == MQTT_CORE_ST_ERROR || MqttRuntime_NeedsModemRecovery(); }
// 判断 MQTT 维护流程是否禁止自动模块恢复。试连、恢复和阻塞阶段按状态限制自动重启，避免覆盖正在进行的持久化或回退事务。
static int app_maintenance_busy(void *context)
{
    ABoxMqttTrialState state = MqttTrialManager_State(); (void)context;
    return !MqttTrialManager_RuntimeRecoveryAllowed() || (state != ABOX_MQTT_TRIAL_IDLE && state != ABOX_MQTT_TRIAL_COMMITTED && state != ABOX_MQTT_TRIAL_FAILED) ||
        (MqttRuntime_State() == ABOX_MQTT_RUNTIME_BLOCKED && !MqttRuntime_NeedsModemRecovery());
}
// 处理公共 App 服务向产品发出的事件。网络变更事件触发发送状态清理及运动会话撤销，其他事件保持现有处理方式。
static void app_event(void *context, ABoxAppServiceEvent event)
{
    (void)context;
    if (event == ABOX_APP_NETWORK_CHANGED) mqtt_network_changed();
}
// 记录 EC800 恢复重启事件。只写诊断日志，实际电源序列由公共恢复服务执行。
static void app_restarted(void *context, uint32_t now)
{
    (void)context; (void)now;
    APP_LOGW("CLOUD", "MQTT transport blocked; restart EC800");
}
// 同步云就绪状态并安排首次上报与预置。上升沿唤醒心跳、状态报告及延后 OTA 文件预置，下降沿撤销云就绪；不自动恢复运动授权。
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
// 处理云业务发送、维护结果及延后重启。维护响应按试连结果安排，发送调度在此轮询；只有既有重启条件和期限成立时才执行系统复位。
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

// 在 UART DMA 接收回调中登记 EC800 新数据。只接受 USART1 事件，使用 DMA 写位置通知环形接收器；中断内不解析 JSON 或执行云命令。
void CloudService_OnUartRxEvent(UART_HandleTypeDef* huart, uint16_t size)
{
    if ((huart == 0) || (huart->Instance != USART1)) return;
    (void)ABoxEc800Rx_OnDmaEvent(&g_cloud.ec800_rx, size);
}

// 在 EC800 UART 错误时登记接收重启请求。只处理 USART1，将耗时恢复延后到服务调度，避免在中断内执行复杂重连。
void CloudService_OnUartError(UART_HandleTypeDef* huart)
{
    if ((huart == 0) || (huart->Instance != USART1)) return;
    ABoxEc800Rx_RequestRestart(&g_cloud.ec800_rx);
}
