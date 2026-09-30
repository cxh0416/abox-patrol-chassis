#include "ota_service.h"

#include <stdio.h>
#include <string.h>

#include "abox_boot_v2.h"
#include "abox_boot_v2_app.h"
#include "abox_boot_v2_services.h"
#include "abox_boot_v2_ca_pem.h"
#include "app_log.h"
#include "app_version.h"
#include "boot_cfg.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "main.h"
#include "patrol_config.h"

static ABoxBootV2Ec800Adapter g_adapter;

static uint8_t adapter_mqtt_client_index(void *context)
{
    (void)context;
    return MqttRuntime_ActiveClient();
}

static void adapter_log(void *context, uint8_t level, const char *message)
{
    (void)context;
    if (level >= 3U) APP_LOGE("OTA", "%s", message);
    else if (level == 2U) APP_LOGW("OTA", "%s", message);
    else APP_LOGI("OTA", "%s", message);
}

void OtaService_Init(Ec800AtCore_t *at)
{
    static ABoxBootV2AppPort port;
    static ABoxBootV2AppConfig config;
    static const ABoxBootV2Layout layout = {
        BOOT_STATE_A_ADDR, BOOT_STATE_B_ADDR, BOOT_STATE_PAGE_SIZE,
        APP_START_ADDR, APP_END_ADDR, 0x20000000U, 0x20010000U
    };
    const PatrolRemoteConfigV5_t *runtime = PatrolConfig_Get();
    extern uint8_t __app_image_end;

    memset(&port, 0, sizeof(port));
    (void)ABoxBootV2Ec800Adapter_BindServices(&g_adapter, at, MQTT_Core_Bridge(), &port);
    port.mqtt_client_index = adapter_mqtt_client_index;
    port.log = adapter_log;

    memset(&config, 0, sizeof(config));
    config.ota_origin = runtime->ota_origin;
    config.revision_product = "patrol_vehicle_chassis";
    config.artifact_name = "patrol_vehicle_chassis_app.bin";
    config.version_prefix = "patrol-chassis-";
    config.running_version = Patrol_AppFwVersion();
    config.ca_pem = (const uint8_t *)g_ota_ca_pem;
    config.ca_pem_length = (uint32_t)(sizeof(g_ota_ca_pem) - 1U);
    config.app_start_addr = APP_START_ADDR;
    config.app_size = (uint32_t)(&__app_image_end - (uint8_t *)APP_START_ADDR);
    config.app_max_size = APP_MAX_SIZE;
    (void)ABoxBootV2_StateBindLayout(&layout);
    (void)ABoxBootV2App_Init(&port, &config);
}

uint8_t OtaService_BeginProvisioning(void) { return (uint8_t)ABoxBootV2App_BeginProvisioning(); }
void OtaService_Task(void) { ABoxBootV2App_Task(); }
uint8_t OtaService_IsProvisioned(void) { return (uint8_t)ABoxBootV2App_IsProvisioned(); }
uint8_t OtaService_IsReady(void) { return (uint8_t)ABoxBootV2App_IsReady(); }
uint8_t OtaService_IsBusy(void) { return (uint8_t)ABoxBootV2App_IsBusy(); }
const char *OtaService_Phase(void) { return ABoxBootV2App_Phase(); }
uint32_t OtaService_LastError(void) { return ABoxBootV2App_LastError(); }

uint8_t OtaService_Start(const CloudProtocolRequest_t *request)
{
    ABoxBootV2AppRequest common;
    size_t url_length;
    size_t version_length;
    if (!request) return 0U;
    url_length = strlen(request->ota_url);
    version_length = strlen(request->ota_version);
    if (url_length >= sizeof(common.url) || version_length >= sizeof(common.version)) return 0U;
    memset(&common, 0, sizeof(common));
    memcpy(common.url, request->ota_url, url_length + 1U);
    memcpy(common.version, request->ota_version, version_length + 1U);
    common.size = request->ota_size;
    common.crc32 = request->ota_crc32;
    return (uint8_t)ABoxBootV2App_Start(&common);
}

uint8_t OtaService_TakeInstallReady(void) { return (uint8_t)ABoxBootV2App_TakeInstallReady(); }
uint8_t OtaService_TakeFailure(uint32_t *error) { return (uint8_t)ABoxBootV2App_TakeFailure(error); }
void OtaService_AcknowledgeRollback(void) { (void)ABoxBootV2_AcknowledgeRollback(); }
const char *OtaService_TargetVersion(void) { return ABoxBootV2App_TargetVersion(); }
