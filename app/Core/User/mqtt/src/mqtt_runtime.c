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
// 查询 TLS/MQTT 运行时是否要求模块恢复。仅提供恢复意图，实际电源与重启时机由 App 服务统一仲裁。
uint8_t MqttRuntime_NeedsModemRecovery(void)
{ return ABoxMqttRuntime_NeedsModemRecovery(&g_runtime); }
static uint32_t g_runtime_now;
static const ABoxMqttRuntimeCoreApi g_core_api = {
  MQTT_Core_SetPaused, MQTT_Core_SetSecurityReady,
  MQTT_Core_ApplyRuntimeConfig, MQTT_Core_Task, MQTT_Core_Reset,
  MQTT_Core_Stop, MQTT_Core_IsReady, MQTT_Core_IsConnected
};
// 检查 TLS 证书文件预置完成且 OTA 当前空闲。这是公共运行时的产品端口回调，避免使用正在被升级流程改动的共享资源。
static uint8_t ca_ready(void *u) {
  (void)u;
  return OtaService_IsProvisioned() && !OtaService_IsBusy();
}
// 报告是否已有可用的可信 UTC 时间来源。供 TLS 运行前提检查，不能用 MCU 上电时间代替证书有效期所需绝对时间。
static uint8_t time_valid(void *u) {
  (void)u;
  return TimeBase_IsValid();
}
// 查询 MQTT 试连管理是否允许自动恢复。恢复失败锁定期间禁止自动重启掩盖原始持久化问题。
static uint8_t recovery_allowed(void *u) {
  (void)u;
  return MqttTrialManager_RuntimeRecoveryAllowed();
}
// 把模块时钟相关 AT 行交给可信时间解析。仅观察完整行，不在此修改 MQTT 或运动状态机。
static void observe_line(void *u, const char *l) {
  (void)u;
  TimeBase_UpdateFromCCLK(l);
}
// 在 MQTT 运行配置切换前撤销发送与运动旧状态。先通知云发送器网络变化，再清空共享控制会话及重放缓存。
static void revoke(void *u) {
  (void)u;
  CloudTx_NetworkChanged();
  RemoteService_RevokeForNetworkChange();
}
// 处理公共 TLS/MQTT 运行时阶段变化。记录阶段并向试连管理器转发事件；失败维持安全连接策略，不降级为未经允许的明文连接。
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
// 绑定公共运行时、MQTT 核心端口与安全检查回调。加载标准 EC800 选项并启用模块时钟模式查询，产品仅提供配置与状态适配。
int MqttRuntime_Init(ABoxEc800At *at, const ABoxMqttConfig *c, uint32_t now) {
  ABoxMqttRuntimePort p;
  ABoxMqttRuntimeOptions o;
  memset(&p, 0, sizeof(p));
  if (!ABoxMqttRuntimePort_BindCore(&p, &g_core_api)) return 0;
  ABoxMqttRuntimeOptions_V4Ec800(&o, MQTT_RUNTIME_OWNER,
                                        PATROL_MQTT_TLS_PROFILE_DEFAULT);
  o.query_clock_mode = 1U;
  p.ca_ready = ca_ready;
  p.time_valid = time_valid;
  p.recovery_allowed = recovery_allowed;
  p.observe_modem_line = observe_line;
  p.before_network_change = revoke;
  p.state_changed = changed;
#if PATROL_MQTT_TRIAL_FAULT_INJECTION && PATROL_MQTT_TRIAL_FAULT_KIND == 1
  o.subscribe_timeout_ms = 180000;
#endif
  return ABoxMqttRuntime_Init(&g_runtime, at, &p, &o, c, now);
}
// 停止首次注册试连建立的运行连接。返回公共停止接口的进度值，调用方在异步恢复阶段继续轮询，不能当作普通布尔完成标志。
int MqttRuntime_StopFirst(void) {
  return ABoxMqttRuntime_StopFirst(&g_runtime, g_runtime_now);
}
// 按当前单调时间轮询 TLS/MQTT 运行状态机。同时更新供回调使用的时间基准，由云任务串行调度。
void MqttRuntime_Task(uint32_t now) { g_runtime_now = now; ABoxMqttRuntime_Poll(&g_runtime, now); }
// 校验并暂存一份仅 RAM 的候选运行配置。此时尚未激活连接，也不写 Flash，便于先回复旧连接上的维护请求。
int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *c) {
  return ABoxMqttRuntime_Stage(&g_runtime, c);
}
// 在适当响应时序后激活已暂存候选。更新运行时间基准并启动公共切换流程，结果仍需连接、订阅与发布证明。
int MqttRuntime_ActivateStagedCandidate(uint32_t now) {
  g_runtime_now = now;
  return ABoxMqttRuntime_Activate(&g_runtime, now);
}
// 提交并开始应用 RAM 候选运行配置。复用公共运行时，不在此保存为正式 Config V5。
int MqttRuntime_ApplyRamCandidate(const ABoxMqttConfig *c, uint32_t now) {
  g_runtime_now = now;
  return ABoxMqttRuntime_Apply(&g_runtime, c, now);
}
// 请求恢复指定的稳定 RAM 运行配置。恢复也是异步连接过程，最终证明交给试连管理器，失败不能默认为已恢复。
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *c, uint32_t now) {
  g_runtime_now = now;
  return ABoxMqttRuntime_Restore(&g_runtime, c, now);
}
// 在配置恢复失败时锁定运行时。阻止自动连接继续掩盖不一致配置，等待明确的维护恢复路径。
void MqttRuntime_BlockForRestoreFailure(void) {
  ABoxMqttRuntime_Block(&g_runtime);
}
// 查询公共运行时是否已经进入 READY。表示连接流程达到定义的就绪阶段，不代表候选已持久化提交。
int MqttRuntime_IsReady(void) { return ABoxMqttRuntime_IsReady(&g_runtime); }
// 取得 TLS/MQTT 运行时阶段枚举。用于调度和诊断，状态只能由公共运行时操作推进。
MqttRuntimeState MqttRuntime_State(void) { return ABoxMqttRuntime_GetState(&g_runtime); }
// 读取当前使用的 EC800 MQTT client 索引。索引用于 OTA 等服务与当前连接共享资源，不是布尔结果。
uint8_t MqttRuntime_ActiveClient(void) {
  return ABoxMqttRuntime_ActiveClient(&g_runtime);
}
// 查询当前活动连接是否采用 TLS。反映实际运行时选择，不等同于配置中期望开启 TLS 的字段。
uint8_t MqttRuntime_ActiveTls(void) { return ABoxMqttRuntime_IsTlsActive(&g_runtime); }
// 读取当前活动 PDP/TLS 上下文索引。供共享模块服务正确选择资源，返回的是索引编号而非是否成功。
uint8_t MqttRuntime_ActiveContext(void) {
  return ABoxMqttRuntime_ActiveContext(&g_runtime);
}
// 通知运行时底层模块已经复位。清理旧模块连接假设，后续按统一状态机恢复，不能保留原连接就绪证明。
void MqttRuntime_OnModemReset(void) {
  ABoxMqttRuntime_OnModemReset(&g_runtime, 0);
}
// 返回当前活动运行配置的只读视图。内含运行时持有的字符串，调用者若需跨切换保留必须复制，不能修改或释放内部存储。
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void) {
  return ABoxMqttRuntime_ActiveConfig(&g_runtime);
}
// 返回已暂存但尚未激活配置的只读视图。仅用于试连管理器复制与核验，指针不代表该配置已经成功连接。
const ABoxMqttConfig *MqttRuntime_StagedConfig(void) {
  return ABoxMqttRuntime_StagedConfig(&g_runtime);
}
