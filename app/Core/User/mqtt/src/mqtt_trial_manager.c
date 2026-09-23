#include "mqtt_trial_manager.h"
#include <string.h>
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "app_log.h"

typedef struct {
    ABoxMqttTrial trial;
    ABoxMqttConfig stable, candidate;
    char host[64], username[32], password[64];
    char candidate_host[64], candidate_username[32], candidate_password[64];
    uint32_t now, proof_sequence;
    uint8_t initialized, restore_waiting, cancel_pending;
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
static int prepare(void*u,uint64_t s,const ABoxMqttConfig*c)
{(void)u;(void)s;(void)c;return MqttRuntime_ActivateStagedCandidate(g_trial.now);}
static int noop(void*u,uint64_t s,const ABoxMqttConfig*c)
{(void)u;(void)s;(void)c;return 1;}
static int restore(void*u,uint64_t s,const ABoxMqttConfig*c)
{
    (void)u;(void)s;
    g_trial.restore_waiting=1;
    g_trial.proof_sequence=MQTT_Core_GetPubOkSeq();
#if PATROL_MQTT_TRIAL_FAULT_INJECTION && PATROL_MQTT_TRIAL_FAULT_KIND == 3
    MqttRuntime_BlockForRestoreFailure();
    return 0;
#else
    return MqttRuntime_RestoreRamActive(c,g_trial.now);
#endif
}
void MqttTrialManager_Init(void)
{
    ABoxMqttTrialPort p={&g_trial,prepare,noop,noop,noop,noop,restore};
    memset(&g_trial,0,sizeof(g_trial));
    if(ABoxMqttTrial_Init(&g_trial.trial,&p))g_trial.initialized=1;
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
    g_trial.now=now;g_trial.cancel_pending=0U;
    return ABoxMqttTrial_Start(&g_trial.trial,&g_trial.stable,&g_trial.candidate,proof,now,120000U,90000U);
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
void MqttTrialManager_Poll(uint32_t now)
{
    ABoxMqttTrialState s;
    if(!g_trial.initialized)return;
    g_trial.now=now; s=g_trial.trial.state; ABoxMqttTrial_Poll(&g_trial.trial,now);
    if(s!=g_trial.trial.state)
        APP_LOGI("MQTT_TRIAL","session=%lu state=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
                 (unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
    s=g_trial.trial.state;
    if(s>=ABOX_MQTT_TRIAL_PREPARING&&s<=ABOX_MQTT_TRIAL_VERIFYING&&MqttRuntime_State()==MQTT_RUNTIME_FAILED){ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_ERROR,0,now);return;}
    if(g_trial.trial.state==ABOX_MQTT_TRIAL_COMMITTING){
        if(copy_stable(MqttRuntime_ActiveConfig()))ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_SAVED,0,now);
        else ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_ERROR,0,now);
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
    }else if(g_trial.trial.state==ABOX_MQTT_TRIAL_RESTORING&&g_trial.restore_waiting){
        g_trial.restore_waiting=0;
        ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,ABOX_MQTT_TRIAL_RESTORED,0,now);
        APP_LOGI("MQTT_TRIAL","restore proof session=%lu trial=%u",(unsigned long)(uint32_t)g_trial.trial.session,
                 (unsigned)g_trial.trial.state);
    }
}
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState state,uint32_t now)
{
    ABoxMqttTrialEvent event;
    if(!g_trial.initialized)return;
    if(state==ABOX_MQTT_RUNTIME_PREPARED)event=ABOX_MQTT_TRIAL_PREPARED;
    else if(state==ABOX_MQTT_RUNTIME_CONNECTED)event=ABOX_MQTT_TRIAL_CONNECTED;
    else if(state==ABOX_MQTT_RUNTIME_READY){
        event=ABOX_MQTT_TRIAL_SUBSCRIBED;
        g_trial.proof_sequence=MQTT_Core_GetPubOkSeq();
    }else if(state==ABOX_MQTT_RUNTIME_FAILED)event=ABOX_MQTT_TRIAL_ERROR;
    else return;
    ABoxMqttTrial_Event(&g_trial.trial,g_trial.trial.session,event,0,now);
    APP_LOGI("MQTT_TRIAL","session=%lu runtime=%u trial=%u reason=%u",(unsigned long)(uint32_t)g_trial.trial.session,
             (unsigned)state,(unsigned)g_trial.trial.state,(unsigned)g_trial.trial.reason);
}
ABoxMqttTrialState MqttTrialManager_State(void){return g_trial.trial.state;}
uint64_t MqttTrialManager_Session(void){return g_trial.trial.session;}
