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

static uint8_t copy_text(char *dst, uint16_t size, const char *src)
{ size_t n;if(!dst||!size||!src)return 0;n=strlen(src);if(n>=size)return 0;memcpy(dst,src,n+1);return 1; }
static uint8_t copy_stable(const ABoxMqttConfig *c)
{
    if(!c||!copy_text(g_trial.host,sizeof(g_trial.host),c->host)||
       !copy_text(g_trial.username,sizeof(g_trial.username),c->username)||
       !copy_text(g_trial.password,sizeof(g_trial.password),c->password)) return 0;
    g_trial.stable=*c;g_trial.stable.host=g_trial.host;g_trial.stable.username=g_trial.username;
    g_trial.stable.password=g_trial.password;return 1;
}
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
static int write_config_snapshot(void *context, const void *snapshot)
{
    const MqttSnapshot *value = snapshot;
    (void)context;
    return PatrolConfig_WriteMqttSnapshot(value->config, value->credentials);
}
static int prepare(void*u,uint64_t s,const ABoxMqttConfig*c)
{
    (void)u;(void)s;(void)c;
    if (!MqttRuntime_ActivateStagedCandidate(g_trial.now)) return 0;
    return !g_trial.persistent || CloudService_ApplyMqttIdentity(g_trial.candidate_vid);
}
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
void MqttTrialManager_Init(void)
{
    memset(&g_trial,0,sizeof(g_trial));
    if(ABoxMqttTrialExecutor_Init(&g_trial.executor,&g_trial.trial))g_trial.initialized=1;
}
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
int MqttTrialManager_StagePersistent(const PatrolRemoteConfigV5_t *candidate, uint64_t proof, uint32_t now)
{
    return MqttTrialManager_StagePersistentPsk(candidate, PatrolConfig_MqttCredentials(), proof, now);
}
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
void MqttTrialManager_Accepted(uint32_t now)
{g_trial.now=now;ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_ACCEPTED,0,now);}
void MqttTrialManager_DeferCancel(void){g_trial.cancel_pending=1U;}
void MqttTrialManager_Cancel(uint32_t now)
{
    g_trial.now=now;g_trial.cancel_pending=0U;ABoxMqttTrial_Cancel(&g_trial.trial,now);
    APP_LOGI("MQTT_TRIAL","cancel session=%lu trial=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
             (unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
}
static int maintenance_action(void *context, ABoxMqttTrialExecutorAction action,
    uint64_t session, const ABoxMqttConfig *config, uint32_t now)
{
    (void)context; (void)now;
    if (action == ABOX_MQTT_TRIAL_ACTION_PREPARE) return prepare(&g_trial, session, config);
    if (action == ABOX_MQTT_TRIAL_ACTION_RESTORE) return restore(&g_trial, session, config);
    return 1;
}
static const ABoxMaintenanceServicePort maintenance_port = {0, maintenance_action};
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
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState state,uint32_t now)
{
    if(!g_trial.initialized)return;
    if (g_trial.first && g_trial.trial.state == ABOX_MQTT_TRIAL_RESTORING) return;
    if (state == ABOX_MQTT_RUNTIME_READY) g_trial.proof_sequence=MQTT_Core_GetPubOkSeq();
    if (ABoxMaintenanceService_RuntimeEvent(&g_trial.trial, &g_trial.executor, state, now) < 0) return;
    APP_LOGI("MQTT_TRIAL","session=%lu runtime=%u trial=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
             (unsigned)state,(unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
}
ABoxMqttTrialState MqttTrialManager_State(void){return g_trial.trial.state;}
uint64_t MqttTrialManager_Session(void){return g_trial.trial.session;}
