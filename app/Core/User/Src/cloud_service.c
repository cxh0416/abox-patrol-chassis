#include "cloud_service.h"
#include "cloud_internal.h"
#include <stdio.h>
#include <string.h>
#include "app_log.h"
#include "ec_power.h"
#include "mqtt_core.h"
#include "ota_service.h"
#include "usart.h"
#include "vcu_config.h"

CloudContext g_cloud;

uint32_t CloudPort_Now(void) { return HAL_GetTick(); }

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

static void ec800_rx_sink(void *context, const uint8_t *data, uint16_t length)
{
    EC800_AT_FeedRx((Ec800AtCore_t *)context, data, length);
}

void CloudService_Init(void)
{
    memset(&g_cloud.telemetry, 0, sizeof(g_cloud.telemetry));
    MQTT_CoreConfig_t mqtt;
    const VcuRemoteConfig_t* cfg = VcuConfig_Get();
    memset(&mqtt, 0, sizeof(mqtt));
    (void)ABoxEc800Rx_Init(&g_cloud.ec800_rx, g_cloud.uart_dma, sizeof(g_cloud.uart_dma),
                           g_cloud.rx_ring, sizeof(g_cloud.rx_ring));
    g_cloud.tx_kind = CLOUD_TX_NONE;
    g_cloud.response_pending = 0U;
    g_cloud.log_active = 0U;
    g_cloud.alert_pending = 0U;
    g_cloud.reboot_pending = 0U;
    g_cloud.ota_begin_pending = 0U;
    g_cloud.last_request_id[0] = '\0';
    g_cloud.last_response_payload[0] = '\0';
    g_cloud.reset_reason = read_reset_reason();
    g_cloud.config_ready = VcuConfig_IsCloudReady(cfg);

    {
        const ABoxEc800AtPort at_port = {&huart1, ec800_tick, ec800_write, 0};
        (void)EC800_AT_Init(&g_cloud.at_core, &at_port);
    }
    OtaService_Init(&g_cloud.at_core);
    mqtt.client_idx = 0U;
    if (cfg != 0)
    {
        CloudMessages_CopyString(mqtt.server_host, sizeof(mqtt.server_host), cfg->mqtt_host);
        mqtt.server_port = cfg->mqtt_port;
        CloudMessages_CopyString(mqtt.username, sizeof(mqtt.username), cfg->mqtt_username);
        CloudMessages_CopyString(mqtt.password, sizeof(mqtt.password), cfg->mqtt_password);
        (void)snprintf(mqtt.client_id, sizeof(mqtt.client_id), "patrol_vehicle_chassis_%s", cfg->vid);
        (void)snprintf(g_cloud.topic_heartbeat, sizeof(g_cloud.topic_heartbeat), "/zxwl/abox/%s/heartbeat", cfg->vid);
        (void)snprintf(g_cloud.topic_status_report, sizeof(g_cloud.topic_status_report), "/zxwl/abox/%s/status/report", cfg->vid);
        (void)snprintf(g_cloud.topic_config_cmd, sizeof(g_cloud.topic_config_cmd), "/zxwl/abox/%s/internal/config", cfg->vid);
        (void)snprintf(g_cloud.topic_config_resp, sizeof(g_cloud.topic_config_resp), "/zxwl/abox/%s/internal/config/resp", cfg->vid);
        (void)snprintf(g_cloud.topic_ota_cmd, sizeof(g_cloud.topic_ota_cmd), "/zxwl/abox/%s/internal/ota", cfg->vid);
        (void)snprintf(g_cloud.topic_ota_resp, sizeof(g_cloud.topic_ota_resp), "/zxwl/abox/%s/internal/ota/resp", cfg->vid);
        (void)snprintf(g_cloud.topic_log, sizeof(g_cloud.topic_log), "/zxwl/abox/%s/internal/log", cfg->vid);
        (void)snprintf(g_cloud.topic_control_cmd, sizeof(g_cloud.topic_control_cmd), "/zxwl/abox/%s/ctrl", cfg->vid);
        (void)snprintf(g_cloud.topic_control_resp, sizeof(g_cloud.topic_control_resp), "/zxwl/abox/%s/ctrl/resp", cfg->vid);
        CloudMessages_CopyString(mqtt.sub_topics[0], sizeof(mqtt.sub_topics[0]), g_cloud.topic_config_cmd);
        CloudMessages_CopyString(mqtt.sub_topics[1], sizeof(mqtt.sub_topics[1]), g_cloud.topic_ota_cmd);
        CloudMessages_CopyString(mqtt.sub_topics[2], sizeof(mqtt.sub_topics[2]), g_cloud.topic_control_cmd);
        mqtt.sub_topic_count = 3U;
        CloudMessages_CopyString(mqtt.pub_topic, sizeof(mqtt.pub_topic), g_cloud.topic_heartbeat);
    }
    MQTT_Core_Init(&mqtt, CloudCommands_OnMessage);
    MQTT_Core_SetAtCore(&g_cloud.at_core);

    EC_Power_Init();
    EC_Power_StartOnSequence();
    uart_start_rx();
    g_cloud.heartbeat_due = HAL_GetTick();
    g_cloud.status_report_due = HAL_GetTick();
    g_cloud.status_report_sequence = 1U;
    memset(&g_cloud.status_snapshot, 0, sizeof(g_cloud.status_snapshot));
    g_cloud.iccid_due = HAL_GetTick() + 1000U;
    g_cloud.initialized = 1U;
    if (g_cloud.config_ready) APP_LOGI("CLOUD", "configured vid=%s broker=%s:%u", cfg->vid, cfg->mqtt_host, cfg->mqtt_port);
    else APP_LOGW("CLOUD", "cloud disabled: provision config page");
}

void CloudService_Task(void)
{
    uint8_t ready;
    if (!g_cloud.initialized) return;
    EC_Power_Task();
    (void)ABoxEc800Rx_Drain(&g_cloud.ec800_rx, ec800_rx_sink, &g_cloud.at_core, 0U);
    EC800_AT_SetRxOverflowCount(&g_cloud.at_core,
                                ABoxEc800Rx_OverflowCount(&g_cloud.ec800_rx));
    if (ABoxEc800Rx_TakeRestartRequest(&g_cloud.ec800_rx))
    {
        ABoxEc800At_AbortAll(&g_cloud.at_core, ABOX_EC800_RESULT_ERROR);
        (void)HAL_UART_DMAStop(&huart1);
        ABoxEc800Rx_AfterRestart(&g_cloud.ec800_rx);
        uart_start_rx();
    }
    EC800_AT_Task(&g_cloud.at_core);
    OtaService_Task();
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
    if (!g_cloud.config_ready || !EC_Power_IsOnDone()) return;
    MQTT_Core_Task();
    ready = MQTT_Core_IsReady();
    if (ready && !g_cloud.mqtt_was_ready)
    {
        g_cloud.heartbeat_due = HAL_GetTick();
        g_cloud.status_report_due = HAL_GetTick();
        g_cloud.ota_begin_due = HAL_GetTick() + 1000U;
        g_cloud.ota_begin_pending = 1U;
        APP_LOGI("CLOUD", "mqtt ready");
    }
    else if (!ready && g_cloud.mqtt_was_ready) APP_LOGW("CLOUD", "mqtt disconnected");
    g_cloud.mqtt_was_ready = ready;
    CloudTx_Service();
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
