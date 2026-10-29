#include "abox_maintenance_service.h"
#include "mqtt_trial_manager.h"
#include "mqtt_trial_persistent.h"
#include "abox_mqtt_trial_executor.h"
#include "abox_config_store.h"
#include <string.h>
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "app_log.h"
#include "cloud_service.h"
#include "mqtt_config_adapter.h"

typedef struct {
    ABoxMqttTrial trial;
    ABoxMqttTrialExecutor executor;
    ABoxMqttConfig stable, candidate;
    char host[64], username[32], password[64];
    char candidate_host[64], candidate_username[32], candidate_password[64];
    uint32_t now, proof_sequence;
    uint8_t initialized, restore_waiting, cancel_pending;
    uint8_t persistent, first, flash_restore_failed;
    char candidate_vid[32];
} PatrolMqttTrialManager;
static PatrolMqttTrialManager g_trial;
// 判断是否允许自动运行时或模块恢复。Flash 恢复失败或试连处于 RESTORE_FAILED 时返回不允许，防止自动重连掩盖配置故障。
uint8_t MqttTrialManager_RuntimeRecoveryAllowed(void)
{ return !g_trial.flash_restore_failed && g_trial.trial.state != ABOX_MQTT_TRIAL_RESTORE_FAILED; }

// 完整复制一段候选配置文本。拒绝空参数和容量不足，不以截断字符串作为合法 Broker 或凭据。
static uint8_t copy_text(char *dst, uint16_t size, const char *src)
{ size_t n;if(!dst||!size||!src)return 0;n=strlen(src);if(n>=size)return 0;memcpy(dst,src,n+1);return 1; }
// 复制稳定 MQTT 配置及其引用字符串。将结构中的字符串指针改为管理器自有存储，保证候选切换期间稳定快照仍可用于恢复。
static uint8_t copy_stable(const ABoxMqttConfig *c)
{
    if(!c||!copy_text(g_trial.host,sizeof(g_trial.host),c->host)||
       !copy_text(g_trial.username,sizeof(g_trial.username),c->username)||
       !copy_text(g_trial.password,sizeof(g_trial.password),c->password)) return 0;
    g_trial.stable=*c;g_trial.stable.host=g_trial.host;g_trial.stable.username=g_trial.username;
    g_trial.stable.password=g_trial.password;return 1;
}
// 复制候选 MQTT 配置及其引用字符串。使用独立候选存储，不与稳定配置共用会被覆盖的字符串缓冲。
static uint8_t copy_candidate(const ABoxMqttConfig *c)
{
    if(!c||!copy_text(g_trial.candidate_host,sizeof(g_trial.candidate_host),c->host)||
       !copy_text(g_trial.candidate_username,sizeof(g_trial.candidate_username),c->username)||
       !copy_text(g_trial.candidate_password,sizeof(g_trial.candidate_password),c->password))return 0;
    g_trial.candidate=*c;
    g_trial.candidate.host=g_trial.candidate_host;
    g_trial.candidate.username=g_trial.candidate_username;
    g_trial.candidate.password=g_trial.candidate_password;
    return 1;
}
typedef struct { const PatrolRemoteConfigV5_t *config; const ABoxTlsCredentials *credentials; } MqttSnapshot;
// 把维护服务快照写入产品 Config V5 和凭据存储。作为公共配置提交回调，返回真实保存结果，外层管理器据此决定提交或恢复。
static int write_config_snapshot(void *context, const void *snapshot)
{
    const MqttSnapshot *value = snapshot;
    (void)context;
    return PatrolConfig_WriteMqttSnapshot(value->config, value->credentials);
}
// 激活已暂存的候选连接并按需切换身份。仅在运行时接受激活后继续应用持久候选 VID；不在该阶段宣称候选发布证明成功。
static int prepare(void*u,uint64_t s,const ABoxMqttConfig*c)
{
    (void)u;(void)s;(void)c;
    if (!MqttRuntime_ActivateStagedCandidate(g_trial.now)) return 0;
    return !g_trial.persistent || CloudService_ApplyMqttIdentity(g_trial.candidate_vid);
}
// 按试连类型恢复稳定连接或停止首次连接。记录恢复等待和证明基准，Flash 恢复失败时锁定运行时；首次注册与已有稳定连接使用不同恢复流程。
static int restore(void*u,uint64_t s,const ABoxMqttConfig*c)
{
    (void)u;(void)s;
    g_trial.restore_waiting=1;
    g_trial.proof_sequence=MQTT_Core_GetPubOkSeq();
    if (g_trial.flash_restore_failed) {
        if (g_trial.persistent) (void)CloudService_ApplyMqttIdentity(PatrolConfig_Get()->vid);
        MqttRuntime_BlockForRestoreFailure();
        return 0;
    }
    if (g_trial.first) {
        if (!CloudService_ApplyMqttIdentity(PatrolConfig_Get()->vid)) return 0;
        return MqttRuntime_StopFirst() >= 0;
    }
#if PATROL_MQTT_TRIAL_FAULT_INJECTION && PATROL_MQTT_TRIAL_FAULT_KIND == 3
    MqttRuntime_BlockForRestoreFailure();
    return 0;
#else
    if (!MqttRuntime_RestoreRamActive(c,g_trial.now)) return 0;
    return !g_trial.persistent || CloudService_ApplyMqttIdentity(PatrolConfig_Get()->vid);
#endif
}
// 初始化产品试连上下文并绑定公共执行器。清空候选、稳定快照与恢复标志，公共执行器初始化成功后才接受新试连。
void MqttTrialManager_Init(void)
{
    memset(&g_trial,0,sizeof(g_trial));
    if(ABoxMqttTrialExecutor_Init(&g_trial.executor,&g_trial.trial))g_trial.initialized=1;
}
// 校验并建立一轮 RAM 候选试连。拒绝并行维护或恢复失败状态，保存稳定与候选快照并设置证明及超时窗口，尚不切换连接。
int MqttTrialManager_Stage(const ABoxMqttConfig*c,uint64_t proof,uint32_t now)
{
    if(!g_trial.initialized||!proof||
       (g_trial.trial.state>=ABOX_MQTT_TRIAL_WAIT_ACCEPT&&
        g_trial.trial.state<=ABOX_MQTT_TRIAL_RESTORING)||
       g_trial.trial.state==ABOX_MQTT_TRIAL_RESTORE_FAILED||
       (!g_trial.stable.host&&!copy_stable(MqttRuntime_ActiveConfig()))||
       !MqttRuntime_StageRamCandidate(c)||
       !copy_candidate(MqttRuntime_StagedConfig()))return 0;
    g_trial.now=now;g_trial.cancel_pending=0U;g_trial.persistent=0U;g_trial.first=0U;g_trial.flash_restore_failed=0U;
    return ABoxMqttTrial_Start(&g_trial.trial,&g_trial.stable,&g_trial.candidate,proof,now,120000U,90000U);
}
// 建立同时验证 Config V5 和 TLS 凭据的持久候选。先校验身份、云配置和凭据，再暂存运行配置；只有后续证明与提交阶段完成才写入稳定配置。
int MqttTrialManager_StagePersistentPsk(const PatrolRemoteConfigV5_t *candidate,
    const ABoxTlsCredentials *credentials, uint64_t proof, uint32_t now)
{
    ABoxMqttConfig runtime, stable_runtime;
    const PatrolRemoteConfigV5_t *stable = PatrolConfig_Get();
    if (!candidate || !stable || !PatrolConfig_IsVidValid(candidate->vid) ||
        !ABoxTlsCredentials_Valid(credentials) ||
        !PatrolConfig_IsCloudReady(candidate) || !PatrolMqttConfig_FromV5(candidate, &runtime) ||
        !PatrolMqttConfig_FromV5(stable, &stable_runtime)) return 0;
    runtime.tls_credentials = *credentials;
    if (!MqttTrialManager_Stage(&runtime, proof, now)) return 0;
    memcpy(g_trial.candidate_vid, candidate->vid, sizeof(g_trial.candidate_vid));
    (void)copy_stable(&stable_runtime);
    g_trial.persistent = 1U;
    return 1;
}
// 使用当前 TLS 凭据建立持久 MQTT 配置候选。复用带凭据的试连入口，避免绕过统一候选与恢复校验。
int MqttTrialManager_StagePersistent(const PatrolRemoteConfigV5_t *candidate, uint64_t proof, uint32_t now)
{
    return MqttTrialManager_StagePersistentPsk(candidate, PatrolConfig_MqttCredentials(), proof, now);
}
// 为首次注册建立专用候选试连。没有既有稳定连接可恢复时标记 first，失败通过停止首次连接退出，不能伪造已有稳定 Broker。
int MqttTrialManager_StartEnrollment(const ABoxMqttConfig *candidate, const char *vid, uint32_t now)
{
    if (!g_trial.initialized || !candidate || !PatrolConfig_IsVidValid(vid) ||
        g_trial.trial.state == ABOX_MQTT_TRIAL_RESTORE_FAILED ||
        (g_trial.trial.state >= ABOX_MQTT_TRIAL_WAIT_ACCEPT &&
         g_trial.trial.state <= ABOX_MQTT_TRIAL_RESTORING) ||
        !MqttRuntime_StageRamCandidate(candidate) ||
        !copy_candidate(MqttRuntime_StagedConfig())) return 0;
    strcpy(g_trial.candidate_vid, vid);
    g_trial.now = now;
    g_trial.cancel_pending = 0U;
    g_trial.persistent = 1U;
    g_trial.first = 1U;
    g_trial.flash_restore_failed = 0U;
    return ABoxMqttTrial_StartFirst(&g_trial.trial, &g_trial.candidate, 1U, now);
}
// 在接受响应发出后通知试连可以继续。向公共试连状态机提交与当前会话关联的 ACCEPTED 事件，而非直接标记已验证。
void MqttTrialManager_Accepted(uint32_t now)
{g_trial.now=now;ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_ACCEPTED,0,now);}
// 登记等待响应完成后再取消试连的意图。保护旧连接上的取消响应时序，真正取消由后续完成回调触发。
void MqttTrialManager_DeferCancel(void){g_trial.cancel_pending=1U;}
// 取消当前试连并进入既有恢复流程。清除延后取消标志并提交公共取消事件，保存状态日志供维护排查。
void MqttTrialManager_Cancel(uint32_t now)
{
    g_trial.now=now;g_trial.cancel_pending=0U;ABoxMqttTrial_Cancel(&g_trial.trial,now);
    APP_LOGI("MQTT_TRIAL","cancel session=%lu trial=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
             (unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
}
// 把公共维护执行器动作分发到产品准备和恢复端口。仅处理定义的 PREPARE 与 RESTORE 适配动作，其余步骤继续由公共服务管理。
static int maintenance_action(void *context, ABoxMqttTrialExecutorAction action,
    uint64_t session, const ABoxMqttConfig *config, uint32_t now)
{
    (void)context; (void)now;
    if (action == ABOX_MQTT_TRIAL_ACTION_PREPARE) return prepare(&g_trial, session, config);
    if (action == ABOX_MQTT_TRIAL_ACTION_RESTORE) return restore(&g_trial, session, config);
    return 1;
}
static const ABoxMaintenanceServicePort maintenance_port = {0, maintenance_action};
// 推进试连执行、证明、持久化提交及恢复。一次轮询按公共维护服务推进，提交 Config V5 与凭据结果必须检查；失败恢复未成功时维持阻塞状态。
void MqttTrialManager_Poll(uint32_t now)
{
    ABoxMqttTrialState s;
    if(!g_trial.initialized)return;
    g_trial.now=now; s=g_trial.trial.state;
    (void)ABoxMaintenanceService_Poll(&g_trial.trial, &g_trial.executor, &maintenance_port, now);
    if(s!=g_trial.trial.state)
        APP_LOGI("MQTT_TRIAL","session=%lu state=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
                 (unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
    s=g_trial.trial.state;
    if (s == ABOX_MQTT_TRIAL_RESTORING && g_trial.first) {
        int stopped = MqttRuntime_StopFirst();
        if (stopped != 0) ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,
            stopped > 0 ? ABOX_MQTT_TRIAL_RESTORED : ABOX_MQTT_TRIAL_ERROR,0,now);
        return;
    }
    if(s>=ABOX_MQTT_TRIAL_PREPARING&&s<=ABOX_MQTT_TRIAL_VERIFYING&&
       (MqttRuntime_State()==MQTT_RUNTIME_FAILED || MqttRuntime_State()==ABOX_MQTT_RUNTIME_BLOCKED)){
        ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_ERROR,0,now);
        return;
    }
    if(g_trial.trial.state==ABOX_MQTT_TRIAL_COMMITTING){
        static const ABoxConfigStorePort store = {0, write_config_snapshot};
        ABoxConfigStoreResult result = ABOX_CONFIG_STORE_SAVED;
        if(g_trial.persistent) {
            PatrolRemoteConfigV5_t next=*PatrolConfig_Get();
            memcpy(next.vid,g_trial.candidate_vid,sizeof(next.vid));
            strcpy(next.mqtt_host,g_trial.candidate.host);
            next.mqtt_port=g_trial.candidate.port;
            strcpy(next.mqtt_username,g_trial.candidate.username);
            strcpy(next.mqtt_password,g_trial.candidate.password);
            next.mqtt_tls_enabled=g_trial.candidate.tls_enabled;
            next.mqtt_tls_profile_id=g_trial.candidate.tls_profile_id;
            if (g_trial.first) next.reserved0=1U;
            const MqttSnapshot candidate = {&next, &g_trial.candidate.tls_credentials};
            const MqttSnapshot stable = {PatrolConfig_Get(), PatrolConfig_MqttCredentials()};
            result=ABoxMaintenanceService_Commit(&g_trial.trial, g_trial.trial.session,
                &store, &candidate, &stable, now);
            if (result == ABOX_CONFIG_STORE_SAVED) PatrolConfig_Load();
        }
        if(result!=ABOX_CONFIG_STORE_SAVED) {
            if(result==ABOX_CONFIG_STORE_RECOVERY_FAILED) g_trial.flash_restore_failed=1U;
            if (!g_trial.persistent)
                ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_ERROR,0,now);
        } else {
            if (!g_trial.persistent)
                ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_SAVED,0,now);
            (void)copy_stable(MqttRuntime_ActiveConfig());
        }
        APP_LOGI("MQTT_TRIAL","commit session=%lu trial=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
                 (unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
    }
}
// 用新的成功心跳发布证明候选或恢复连接。核对初始化、双层就绪、发布序号变化和取消意图，旧连接或旧序号不能完成本轮试连。
void MqttTrialManager_OnHeartbeatProof(uint32_t publish_sequence,uint32_t now)
{
    if(!g_trial.initialized||!MqttRuntime_IsReady()||!MQTT_Core_IsReady()||
       publish_sequence==g_trial.proof_sequence)return;
    if(g_trial.trial.state==ABOX_MQTT_TRIAL_VERIFYING){
        if(g_trial.cancel_pending)return;
#if PATROL_MQTT_TRIAL_FAULT_INJECTION && (PATROL_MQTT_TRIAL_FAULT_KIND == 2 || PATROL_MQTT_TRIAL_FAULT_KIND == 3)
        return;
#else
        ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_PROVED,g_trial.trial.proof,now);
        APP_LOGI("MQTT_TRIAL","heartbeat proof session=%lu trial=%u",(unsigned long)(uint32_t)g_trial.trial.session,
                 (unsigned)g_trial.trial.state);
#endif
    }else if(g_trial.trial.state==ABOX_MQTT_TRIAL_RESTORING&&g_trial.restore_waiting&&!g_trial.first){
        g_trial.restore_waiting=0;
        ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_RESTORED,0,now);
        APP_LOGI("MQTT_TRIAL","restore proof session=%lu trial=%u",(unsigned long)(uint32_t)g_trial.trial.session,
                 (unsigned)g_trial.trial.state);
    }
}
// 将 TLS/MQTT 运行时变化转发给公共维护服务。READY 时重新记录发布证明起点，首次注册恢复阶段使用独立的停止完成路径。
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState state,uint32_t now)
{
    if(!g_trial.initialized)return;
    if (g_trial.first && g_trial.trial.state == ABOX_MQTT_TRIAL_RESTORING) return;
    if (state == ABOX_MQTT_RUNTIME_READY) g_trial.proof_sequence=MQTT_Core_GetPubOkSeq();
    if (ABoxMaintenanceService_RuntimeEvent(&g_trial.trial, &g_trial.executor, state, now) < 0) return;
    APP_LOGI("MQTT_TRIAL","session=%lu runtime=%u trial=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
             (unsigned)state,(unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
}
// 返回当前 MQTT 试连阶段枚举。只读状态查询，用于响应和互斥门禁，不改变试连进度。
ABoxMqttTrialState MqttTrialManager_State(void){return g_trial.trial.state;}
// 返回当前试连的 64 位会话标识。用于关联阶段事件与证明，不可将其用作底盘运动会话授权。
uint64_t MqttTrialManager_Session(void){return g_trial.trial.session;}
