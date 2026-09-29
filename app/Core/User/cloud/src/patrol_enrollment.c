#include "patrol_enrollment.h"

#include <stdio.h>
#include <string.h>

#include "abox_boot_v2.h"
#include "abox_enrollment.h"
#include "abox_enrollment_ec800_http.h"
#include "app_version.h"
#include "cloud_internal.h"
#include "cloud_service.h"
#include "ec_power.h"
#include "main.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "ota_service.h"
#include "patrol_config.h"

#define ENROLL_OWNER (ABOX_EC800_OWNER_PRODUCT_BASE + 2U)

static ABoxEnrollment g_enrollment;
static ABoxEnrollmentEc800Http g_http;
static char g_uid[25];
static char g_vid[32];
static char g_request_id[48];
static char g_poll_token[64];

static int ready(void *user)
{
    (void)user;
    return EC_Power_IsOnDone() && OtaService_IsProvisioned() && !OtaService_IsBusy() &&
           ABoxEnrollmentEc800Http_Ready(&g_http);
}

static int identity(void *user, ABoxEnrollmentIdentity *out)
{
    static ABoxBootV2Descriptor descriptor;
    const PatrolRemoteConfigV5_t *cfg = PatrolConfig_Get();
    const char *boot = "";
    (void)user;
    if (!cfg || !out) return 0;
    (void)snprintf(g_uid, sizeof(g_uid), "%08lX%08lX%08lX",
                   (unsigned long)HAL_GetUIDw0(), (unsigned long)HAL_GetUIDw1(),
                   (unsigned long)HAL_GetUIDw2());
    if (strcmp(cfg->vid, PATROL_CONFIG_DEFAULT_VID) != 0 || cfg->reserved0 == 1U)
        (void)snprintf(g_vid, sizeof(g_vid), "%s", cfg->vid);
    else (void)snprintf(g_vid, sizeof(g_vid), "PATROL-%s", g_uid);
    if (ABoxBootV2_DescriptorRead(&descriptor) && ABoxBootV2_DescriptorValid(&descriptor))
        boot = descriptor.build_version;
    *out = (ABoxEnrollmentIdentity){g_uid, "", g_vid,
                                    "patrol_vehicle_chassis_stm32f105_ec800_v1",
                                    boot, Patrol_AppFwVersion()};
    return 1;
}

static int post(void *user, const char *url, const char *body, char *response, size_t capacity)
{
    (void)user;
    MQTT_Core_SetPaused(1U);
    return ABoxEnrollmentEc800Http_Post(&g_http, url, body, response, capacity);
}

static void http_done(void *user, uint16_t status, size_t length, int cleanup_ok)
{
    (void)user;
    ABoxEnrollment_OnHttp(&g_enrollment, status, length, cleanup_ok, HAL_GetTick());
}

static void cancel_http(void *user)
{ (void)user; ABoxEnrollmentEc800Http_Cancel(&g_http); }

static int start_trial(void *user, const ABoxEnrollmentCredential *credential)
{
    ABoxMqttConfig runtime;
    (void)user;
    if (!credential) return 0;
    runtime = (ABoxMqttConfig){credential->host, credential->port,
                              credential->username, credential->password,
                              credential->tls_enabled, credential->tls_profile_id};
    if (!MqttTrialManager_StartEnrollment(&runtime, credential->vid, HAL_GetTick())) return 0;
    g_cloud.config_ready = 1U;
    return 1;
}

static void cancel_trial(void *user)
{ (void)user; MqttTrialManager_Cancel(HAL_GetTick()); }

void PatrolEnrollment_Init(ABoxEc800At *at)
{
    ABoxEnrollmentPort port = {0, ready, identity, post, cancel_http, start_trial, cancel_trial};
    ABoxEnrollmentBuffers buffers = {0};
    buffers.url = g_cloud.last_response_payload; buffers.url_size = 192U;
    buffers.body = g_cloud.response_payload; buffers.body_size = 512U;
    buffers.response = g_cloud.tx_payload; buffers.response_size = 769U;
    buffers.request_id = g_request_id; buffers.request_id_size = sizeof(g_request_id);
    buffers.poll_token = g_poll_token; buffers.poll_token_size = sizeof(g_poll_token);
    (void)ABoxEnrollmentEc800Http_Init(&g_http, at, ENROLL_OWNER, "UFS:ota_ca.pem", http_done, 0);
    if (ABoxEnrollment_Init(&g_enrollment, &port, &buffers, PatrolConfig_Get()->ota_origin))
        (void)ABoxEnrollment_SetExpectedTlsProfile(&g_enrollment,
                                                    PATROL_MQTT_TLS_PROFILE_DEFAULT);
    if (CloudService_NeedsEnrollment()) MQTT_Core_SetPaused(1U);
}

void PatrolEnrollment_Task(void)
{
    ABoxMqttTrialState trial_state;
    if (!CloudService_NeedsEnrollment() && ABoxEnrollment_State(&g_enrollment) != ABOX_ENROLLMENT_TRIAL)
        return;
    if (ABoxEnrollment_State(&g_enrollment) == ABOX_ENROLLMENT_TRIAL) {
        trial_state = MqttTrialManager_State();
        if (trial_state == ABOX_MQTT_TRIAL_COMMITTED || trial_state == ABOX_MQTT_TRIAL_FAILED) {
            if (trial_state == ABOX_MQTT_TRIAL_FAILED) g_cloud.config_ready = 0U;
            ABoxEnrollment_TrialResult(&g_enrollment,
                trial_state == ABOX_MQTT_TRIAL_COMMITTED, HAL_GetTick());
        }
        return;
    }
    MQTT_Core_SetPaused(1U);
    ABoxEnrollment_Poll(&g_enrollment, HAL_GetTick());
}
