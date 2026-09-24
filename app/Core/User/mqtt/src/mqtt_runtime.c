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
static ABoxEc800At *g_runtime_at;
static int8_t g_first_stop_state;
static void first_stop_done(ABoxEc800Result result, void *user) {
  (void)user;
  g_first_stop_state = result == ABOX_EC800_RESULT_OK ? 2 : -1;
  if (g_first_stop_state == 2) {
    MQTT_Core_SetPaused(1U);
    memset(&g_runtime.active, 0, sizeof(g_runtime.active));
    memset(&g_runtime.requested, 0, sizeof(g_runtime.requested));
    g_runtime.active_host[0] = '\0';
    g_runtime.host[0] = '\0';
    g_runtime.state = ABOX_MQTT_RUNTIME_FAILED;
  }
}
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
static uint8_t mqtt_ready(void *u) {
  (void)u;
  return MQTT_Core_IsReady();
}
static uint8_t mqtt_connected(void *u) {
  MQTT_CoreState_t s;
  (void)u;
  s = MQTT_Core_GetState();
  return s == MQTT_CORE_ST_SUB || s == MQTT_CORE_ST_WAIT_SUB ||
         s == MQTT_CORE_ST_READY;
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
  g_runtime_at = at;
  g_first_stop_state = 0;
  return ABoxMqttRuntime_Init(&g_runtime, at, &p, &o, c, now);
}
int MqttRuntime_StopFirst(void) {
  if (g_first_stop_state == 2) return 1;
  if (g_first_stop_state < 0) return -1;
  if (g_first_stop_state == 1) return 0;
  if (!g_runtime_at) return -1;
  MQTT_Core_SetPaused(1U);
  ABoxEc800At_Cancel(g_runtime_at, MQTT_RUNTIME_OWNER);
  g_runtime.state = ABOX_MQTT_RUNTIME_FAILED;
  if (!ABoxEc800At_Submit(g_runtime_at,
                          g_runtime.active_tls ? "AT+QMTCLOSE=1" : "AT+QMTCLOSE=0",
                          ABOX_EC800_OWNER_PRODUCT_BASE + 5U,
                          ABOX_EC800_PRIORITY_HIGH, 5000U, first_stop_done, 0)) {
    g_first_stop_state = -1;
    return -1;
  }
  g_first_stop_state = 1;
  return 0;
}
void MqttRuntime_Task(uint32_t now) { g_runtime_now = now; ABoxMqttRuntime_Poll(&g_runtime, now); }
int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *c) {
  int staged = ABoxMqttRuntime_Stage(&g_runtime, c);
  if (staged) g_first_stop_state = 0;
  return staged;
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
  MQTT_Core_SetPaused(1U);
  MQTT_Core_SetSecurityReady(0U);
  g_runtime.state = ABOX_MQTT_RUNTIME_FAILED;
}
int MqttRuntime_IsReady(void) { return ABoxMqttRuntime_IsReady(&g_runtime); }
MqttRuntimeState MqttRuntime_State(void) { return g_runtime.state; }
uint8_t MqttRuntime_ActiveClient(void) {
  return ABoxMqttRuntime_ActiveClient(&g_runtime);
}
uint8_t MqttRuntime_ActiveTls(void) { return g_runtime.active_tls; }
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
