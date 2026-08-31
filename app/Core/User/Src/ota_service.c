#include "ota_service.h"

#include <stdio.h>
#include <string.h>

#include "abox_boot_v2.h"
#include "abox_boot_v2_app.h"
#include "abox_boot_v2_ca_pem.h"
#include "app_log.h"
#include "app_version.h"
#include "boot_cfg.h"
#include "mqtt_core.h"
#include "mqtt_core_int.h"
#include "vcu_config.h"

typedef struct {
    Ec800AtCore_t *at;
    ABoxBootV2AtDoneFn done;
    void *done_user;
    ABoxBootV2AtEventFn events;
    void *events_user;
} OtaAdapter_t;

static OtaAdapter_t g_adapter;

static uint32_t adapter_tick(void *context) { (void)context; return HAL_GetTick(); }

static void adapter_done(Ec800AtCmdResult_t result, void *user)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)user;
    ABoxBootV2AtDoneFn done = adapter->done;
    void *done_user = adapter->done_user;
    adapter->done = 0;
    adapter->done_user = 0;
    if (done) done((ABoxBootV2AtResult)result, done_user);
}

static int adapter_submit(void *context, const char *command, uint32_t timeout_ms,
                          ABoxBootV2AtDoneFn done, void *user)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    adapter->done = done;
    adapter->done_user = user;
    if (!EC800_AT_Submit(adapter->at, command, EC800_AT_OWNER_OTA, EC800_AT_PRIORITY_HIGH,
                         timeout_ms, adapter_done, adapter)) {
        adapter->done = 0;
        adapter->done_user = 0;
        return 0;
    }
    return 1;
}

static int adapter_send_payload(void *context, const uint8_t *data, uint16_t length)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    return EC800_AT_SendPayloadForOwner(adapter->at, EC800_AT_OWNER_OTA, data, length) ? 1 : 0;
}

static int adapter_begin_raw(void *context, uint32_t length)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    return EC800_AT_BeginRawRead(adapter->at, EC800_AT_OWNER_OTA, length) ? 1 : 0;
}

static int adapter_has_pending(void *context)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    return EC800_AT_HasPending(adapter->at, EC800_AT_OWNER_OTA) ? 1 : 0;
}

static int adapter_is_active(void *context)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    return EC800_AT_IsActiveOwner(adapter->at, EC800_AT_OWNER_OTA) ? 1 : 0;
}

static void adapter_cancel(void *context)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    EC800_AT_CancelOwner(adapter->at, EC800_AT_OWNER_OTA);
}

static void adapter_event(Ec800AtEvent_t event, const uint8_t *data, uint16_t length, void *user)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)user;
    if (adapter->events) adapter->events((ABoxBootV2AtEvent)event, data, length, adapter->events_user);
}

static void adapter_register(void *context, ABoxBootV2AtEventFn callback, void *user)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    adapter->events = callback;
    adapter->events_user = user;
    EC800_AT_RegisterUrcHandler(adapter->at, EC800_AT_OWNER_OTA, adapter_event, adapter);
}

static void adapter_mqtt_pause(void *context, uint8_t paused) { (void)context; MQTT_Core_SetPaused(paused); }

static void adapter_log(void *context, uint8_t level, const char *message)
{
    (void)context;
    if (level >= 3U) APP_LOGE("OTA", "%s", message);
    else if (level == 2U) APP_LOGW("OTA", "%s", message);
    else APP_LOGI("OTA", "%s", message);
}

static uint32_t adapter_rx_overflow(void *context)
{
    OtaAdapter_t *adapter = (OtaAdapter_t *)context;
    return EC800_AT_RxOverflowCount(adapter->at);
}

void OtaService_Init(Ec800AtCore_t *at)
{
    static ABoxBootV2AppPort port;
    static ABoxBootV2AppConfig config;
    static const ABoxBootV2Layout layout = {
        BOOT_STATE_A_ADDR, BOOT_STATE_B_ADDR, BOOT_STATE_PAGE_SIZE,
        APP_START_ADDR, APP_END_ADDR, 0x20000000U, 0x20010000U
    };
    const VcuRemoteConfig_t *runtime = VcuConfig_Get();
    extern uint8_t __app_image_end;

    memset(&g_adapter, 0, sizeof(g_adapter));
    g_adapter.at = at;
    memset(&port, 0, sizeof(port));
    port.context = &g_adapter;
    port.tick_ms = adapter_tick;
    port.submit = adapter_submit;
    port.send_payload = adapter_send_payload;
    port.begin_raw_read = adapter_begin_raw;
    port.has_pending = adapter_has_pending;
    port.is_active = adapter_is_active;
    port.cancel = adapter_cancel;
    port.register_events = adapter_register;
    port.mqtt_pause = adapter_mqtt_pause;
    port.log = adapter_log;
    port.transfer_buffer = (uint8_t *)g_mqtt_rx_pending_payload;
    port.transfer_buffer_size = sizeof(g_mqtt_rx_pending_payload);
    port.rx_overflow_count = adapter_rx_overflow;

    memset(&config, 0, sizeof(config));
    config.ota_origin = runtime->ota_origin;
    config.revision_product = "patrol_vehicle_chassis";
    config.artifact_name = "patrol_vehicle_chassis_app.bin";
    config.version_prefix = "patrol-chassis-";
    config.running_version = Vcu_AppFwVersion();
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
