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
static ABoxMqttRuntime g_runtime;
static uint32_t g_runtime_now;
static const ABoxMqttRuntimeCoreApi g_core_api = {
  MQTT_Core_SetPaused, MQTT_Core_SetSecurityReady,
  MQTT_Core_ApplyRuntimeConfig, MQTT_Core_Task, MQTT_Core_Reset,
  MQTT_Core_Stop, MQTT_Core_IsReady, MQTT_Core_IsConnected
};
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
  if (!ABoxMqttRuntimePort_BindCore(&p, &g_core_api)) return 0;
  ABoxMqttRuntimeOptions_StandardEc800(&o, MQTT_RUNTIME_OWNER,
                                        PATROL_MQTT_TLS_PROFILE_DEFAULT);
  o.query_clock_mode = 1U;
  p.ca_ready = ca_ready;
  p.time_valid = time_valid;
  p.observe_modem_line = observe_line;
  p.before_network_change = revoke;
  p.state_changed = changed;
#if PATROL_MQTT_TRIAL_FAULT_INJECTION && PATROL_MQTT_TRIAL_FAULT_KIND == 1
  o.subscribe_timeout_ms = 180000;
#endif
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
