#include "mqtt_runtime.h"
#include "app_log.h"
#include "mqtt_core.h"
#include "ota_service.h"
#include "remote_service.h"
#include "mqtt_trial_manager.h"
#include "cloud_service.h"
#include "time_base.h"
#include <string.h>
#define MQTT_RUNTIME_OWNER (ABOX_EC800_OWNER_PRODUCT_BASE + 4U)
#define MQTT_RUNTIME_CA_FILE "UFS:ota_ca.pem"
static ABoxMqttRuntime g_runtime;
static uint32_t g_runtime_now;
static void set_paused(void *u, uint8_t v) {
  (void)u;
  MQTT_Core_SetPaused(v);
}
static void set_security(void *u, uint8_t v) {
  (void)u;
  MQTT_Core_SetSecurityReady(v);
}
static int apply_config(void *u, const ABoxMqttConfig *c) {
  (void)u;
  return MQTT_Core_ApplyRuntimeConfig(c);
}
static void mqtt_task(void *u) {
  (void)u;
  MQTT_Core_Task();
}
static void mqtt_modem_reset(void *u) {
  (void)u;
  MQTT_Core_Reset();
}
static int mqtt_stop(void *u, uint32_t now) {
  (void)u;
  return MQTT_Core_Stop(now);
}
static uint8_t mqtt_ready(void *u) {
  (void)u;
  return MQTT_Core_IsReady();
}
static uint8_t mqtt_connected(void *u) {
  (void)u;
  return MQTT_Core_IsConnected();
}
static uint8_t ca_ready(void *u) {
  (void)u;
  return OtaService_IsProvisioned() && !OtaService_IsBusy();
}
static uint8_t time_valid(void *u) {
  (void)u;
  return TimeBase_IsValid();
}
static void observe_line(void *u, const char *l) {
  (void)u;
  TimeBase_UpdateFromCCLK(l);
}
static void revoke(void *u) {
  (void)u;
  CloudTx_NetworkChanged();
  RemoteService_RevokeForNetworkChange();
}
static void changed(void *u, ABoxMqttRuntimeState s, const char *x) {
  (void)u;
  (void)x;
  APP_LOGI("MQTT_TLS", "runtime state=%u stage=%s", (unsigned)s, x);
  MqttTrialManager_OnRuntimeState(s, g_runtime_now);
  if (s == ABOX_MQTT_RUNTIME_FAILED)
    APP_LOGE("MQTT_TLS", "runtime failed stage=%s; no plaintext fallback", x);
  else if (s == ABOX_MQTT_RUNTIME_READY)
    APP_LOGI("MQTT_TLS",
             "runtime ready client=%u context=%u connected and subscribed",
             (unsigned)ABoxMqttRuntime_ActiveClient(&g_runtime),
             (unsigned)ABoxMqttRuntime_ActiveContext(&g_runtime));
}
int MqttRuntime_Init(ABoxEc800At *at, const ABoxMqttConfig *c, uint32_t now) {
  ABoxMqttRuntimePort p;
  ABoxMqttRuntimeOptions o;
  memset(&p, 0, sizeof(p));
  memset(&o, 0, sizeof(o));
  p.set_paused = set_paused;
  p.set_security_ready = set_security;
  p.apply_config = apply_config;
  p.mqtt_task = mqtt_task;
  p.mqtt_modem_reset = mqtt_modem_reset;
  p.mqtt_stop = mqtt_stop;
  p.mqtt_ready = mqtt_ready;
  p.mqtt_connected = mqtt_connected;
  p.ca_ready = ca_ready;
  p.time_valid = time_valid;
  p.observe_modem_line = observe_line;
  p.before_network_change = revoke;
  p.state_changed = changed;
  o.owner = MQTT_RUNTIME_OWNER;
  o.command_timeout_ms = 5000;
  o.tls_timeout_ms = 30000;
  o.connect_timeout_ms = 60000;
#if PATROL_MQTT_TRIAL_FAULT_INJECTION && PATROL_MQTT_TRIAL_FAULT_KIND == 1
  o.subscribe_timeout_ms = 180000;
#else
  o.subscribe_timeout_ms = 60000;
#endif
  o.ca_file = MQTT_RUNTIME_CA_FILE;
  o.ca_revision = 1;
  o.plain_client = 0;
  o.tls_client = 1;
  o.tls_context = PATROL_MQTT_TLS_CONTEXT;
  o.tls_profile_id = PATROL_MQTT_TLS_PROFILE_DEFAULT;
  o.supported_tls_context_mask = (uint8_t)(1U << PATROL_MQTT_TLS_CONTEXT);
  return ABoxMqttRuntime_Init(&g_runtime, at, &p, &o, c, now);
}
int MqttRuntime_StopFirst(void) {
  return ABoxMqttRuntime_StopFirst(&g_runtime, g_runtime_now);
}
void MqttRuntime_Task(uint32_t now) { g_runtime_now = now; ABoxMqttRuntime_Poll(&g_runtime, now); }
int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *c) {
  return ABoxMqttRuntime_Stage(&g_runtime, c);
}
int MqttRuntime_ActivateStagedCandidate(uint32_t now) {
  g_runtime_now = now;
  return ABoxMqttRuntime_Activate(&g_runtime, now);
}
int MqttRuntime_ApplyRamCandidate(const ABoxMqttConfig *c, uint32_t now) {
  g_runtime_now = now;
  return ABoxMqttRuntime_Apply(&g_runtime, c, now);
}
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *c, uint32_t now) {
  g_runtime_now = now;
  return ABoxMqttRuntime_Restore(&g_runtime, c, now);
}
void MqttRuntime_BlockForRestoreFailure(void) {
  ABoxMqttRuntime_Block(&g_runtime);
}
int MqttRuntime_IsReady(void) { return ABoxMqttRuntime_IsReady(&g_runtime); }
MqttRuntimeState MqttRuntime_State(void) { return ABoxMqttRuntime_GetState(&g_runtime); }
uint8_t MqttRuntime_ActiveClient(void) {
  return ABoxMqttRuntime_ActiveClient(&g_runtime);
}
uint8_t MqttRuntime_ActiveTls(void) { return ABoxMqttRuntime_IsTlsActive(&g_runtime); }
uint8_t MqttRuntime_ActiveContext(void) {
  return ABoxMqttRuntime_ActiveContext(&g_runtime);
}
void MqttRuntime_OnModemReset(void) {
  ABoxMqttRuntime_OnModemReset(&g_runtime, 0);
}
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void) {
  return ABoxMqttRuntime_ActiveConfig(&g_runtime);
}
const ABoxMqttConfig *MqttRuntime_StagedConfig(void) {
  return ABoxMqttRuntime_StagedConfig(&g_runtime);
}
