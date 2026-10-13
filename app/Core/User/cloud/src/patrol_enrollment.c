#include "patrol_enrollment.h"

#include <stdio.h>
#include <string.h>

#include "abox_enrollment.h"
#include "abox_enrollment_service.h"
#include "abox_stm32f105_ec800_identity.h"
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

static ABoxEnrollmentService g_service;
static ABoxEnrollmentIdentityStorage g_identity_storage;
static char g_request_id[48];
static char g_poll_token[64];

static uint32_t now_ms(void *user)
{ (void)user; return HAL_GetTick(); }
static int network_ready(void *user)
{
    (void)user;
    return EC_Power_IsOnDone() && OtaService_IsProvisioned() && !OtaService_IsBusy();
}
static int needs_enrollment(void *user)
{
    (void)user;
#ifdef ABOX_PSK_STATION
    /* Controlled first injection owns enrollment scratch; no HTTP enrollment. */
    return 0;
#else
    return CloudService_NeedsEnrollment();
#endif
}
static void mqtt_pause(void *user, uint8_t pause)
{ (void)user; MQTT_Core_SetPaused(pause); }

static int identity(void *user, ABoxEnrollmentIdentity *out)
{
    const PatrolRemoteConfigV5_t *cfg = PatrolConfig_Get();
    (void)user;
    if (!cfg || !out) return 0;
    return ABoxStm32F105Ec800Identity_Build(out, &g_identity_storage,
        "", cfg->vid,
        (uint8_t)(strcmp(cfg->vid, PATROL_CONFIG_DEFAULT_VID) != 0 ||
                  cfg->reserved0 == 1U), "PATROL-",
        "patrol_vehicle_chassis_stm32f105_ec800_v1",
        Patrol_AppFwVersion());
}

static int start_trial(void *user, const ABoxEnrollmentCredential *credential)
{
    ABoxMqttConfig runtime;
    (void)user;
    if (!credential) return 0;
    runtime = (ABoxMqttConfig){credential->host, credential->port,
                              credential->username, credential->password,
                              credential->tls_enabled, credential->tls_profile_id};
    runtime.tls_credentials = credential->tls_credentials;
    if (!MqttTrialManager_StartEnrollment(&runtime, credential->vid, HAL_GetTick())) return 0;
    g_cloud.config_ready = 1U;
    return 1;
}

static void cancel_trial(void *user)
{ (void)user; MqttTrialManager_Cancel(HAL_GetTick()); }
static int trial_result(void *user)
{
    ABoxMqttTrialState state;
    (void)user;
    state = MqttTrialManager_State();
    if (state == ABOX_MQTT_TRIAL_COMMITTED) return 1;
    if (state == ABOX_MQTT_TRIAL_FAILED || state == ABOX_MQTT_TRIAL_RESTORE_FAILED) {
        g_cloud.config_ready = 0U;
        return 0;
    }
    return -1;
}

uint8_t PatrolEnrollment_Init(ABoxEc800At *at)
{
    ABoxEnrollmentServicePort port = {0, now_ms, network_ready,
        needs_enrollment, identity, mqtt_pause, start_trial, trial_result,
        cancel_trial};
    ABoxEnrollmentBuffers buffers = {0};
    buffers.url = g_cloud.last_response_payload; buffers.url_size = 192U;
    buffers.body = g_cloud.response_payload; buffers.body_size = 512U;
    buffers.response = g_cloud.tx_payload; buffers.response_size = 769U;
    buffers.request_id = g_request_id; buffers.request_id_size = sizeof(g_request_id);
    buffers.poll_token = g_poll_token; buffers.poll_token_size = sizeof(g_poll_token);
    return (uint8_t)ABoxEnrollmentService_Init(&g_service, at, ENROLL_OWNER,
        "UFS:ota_ca.pem", PatrolConfig_Get()->ota_origin, &buffers,
        PATROL_MQTT_TLS_PROFILE_DEFAULT, &port);
}

void PatrolEnrollment_Task(void)
{
    ABoxEnrollmentService_Poll(&g_service);
}

void PatrolEnrollment_AfterModemReset(void)
{
    ABoxEnrollmentService_AfterModemReset(&g_service);
}
