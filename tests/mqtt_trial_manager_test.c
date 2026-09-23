#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt_trial_manager.h"
#include "mqtt_trial_persistent.h"
#include "mqtt_runtime.h"
#include "mqtt_core.h"

static ABoxMqttConfig active = {"plain", 19683, "u", "p", 0, 0};
static ABoxMqttConfig staged;
static uint32_t pub_sequence;
static unsigned activations, restores;
static unsigned saves;
static unsigned fail_save;
static PatrolRemoteConfigV5_t persisted;
static char identity[32];

const PatrolRemoteConfigV5_t *PatrolConfig_Get(void) { return &persisted; }
uint8_t PatrolConfig_IsCloudReady(const PatrolRemoteConfigV5_t *c) { return c && c->vid[0] && c->mqtt_host[0]; }
uint8_t PatrolMqttConfig_FromV5(const PatrolRemoteConfigV5_t *c, ABoxMqttConfig *out)
{
    if (!c || !out) return 0;
    *out = (ABoxMqttConfig){c->mqtt_host,c->mqtt_port,c->mqtt_username,c->mqtt_password,
                            c->mqtt_tls_enabled,c->mqtt_tls_profile_id};
    return 1;
}
uint8_t PatrolConfig_Save(const PatrolRemoteConfigV5_t *c)
{ saves++; if (fail_save) { fail_save=0; return 0; } persisted=*c; return 1; }
uint8_t CloudService_ApplyMqttIdentity(const char *vid)
{ strcpy(identity, vid); return 1; }

int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *c)
{ staged = *c; return 1; }
int MqttRuntime_ActivateStagedCandidate(uint32_t now)
{ (void)now; activations++; return 1; }
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *c, uint32_t now)
{ (void)now; active = *c; restores++; return 1; }
int MqttRuntime_IsReady(void) { return 1; }
MqttRuntimeState MqttRuntime_State(void) { return ABOX_MQTT_RUNTIME_READY; }
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void) { return &active; }
const ABoxMqttConfig *MqttRuntime_StagedConfig(void) { return &staged; }
void MqttRuntime_BlockForRestoreFailure(void) { assert(0); }
uint8_t MQTT_Core_IsReady(void) { return 1; }
uint32_t MQTT_Core_GetPubOkSeq(void) { return pub_sequence; }

static void to_verifying(uint32_t now)
{
    MqttTrialManager_Accepted(now);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_PREPARING);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_PREPARED, now+1);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_CONNECTING);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_CONNECTED, now+2);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_SUBSCRIBING);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_READY, now+3);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_VERIFYING);
}
int main(void)
{
    ABoxMqttConfig tls = {"tls", 19688, "u", "p", 1, 0};
    ABoxMqttConfig plain = {"plain", 19683, "u", "p", 0, 0};
    MqttTrialManager_Init();
    assert(MqttTrialManager_Stage(&tls, 11, 100));
    to_verifying(100);
    assert(!MqttTrialManager_Stage(&plain, 12, 105));
    assert(activations == 1);
    MqttTrialManager_OnHeartbeatProof(pub_sequence, 106);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_VERIFYING);
    active = tls;
    MqttTrialManager_OnHeartbeatProof(++pub_sequence, 107);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_COMMITTING);
    MqttTrialManager_Poll(108);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_COMMITTED);

    assert(MqttTrialManager_Stage(&plain, 13, 200));
    to_verifying(200);
    MqttTrialManager_Cancel(204);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_RESTORING && restores == 1);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_READY, 205);
    MqttTrialManager_OnHeartbeatProof(pub_sequence, 206);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_RESTORING);
    MqttTrialManager_OnHeartbeatProof(++pub_sequence, 207);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_FAILED);
    assert(strcmp(active.host, "tls") == 0);
    memset(&persisted, 0, sizeof(persisted));
    strcpy(persisted.vid, "OLD");
    strcpy(persisted.mqtt_host, "plain");
    strcpy(persisted.mqtt_username, "u");
    strcpy(persisted.mqtt_password, "p");
    strcpy(persisted.diag_log_host, "diag.example");
    strcpy(persisted.ota_origin, "https://ota.example");
    persisted.steering_center_count = 12345;
    persisted.mqtt_port = 19683;
    active = plain;
    MqttTrialManager_Init();
    PatrolRemoteConfigV5_t candidate = persisted;
    strcpy(candidate.vid, "NEW");
    strcpy(candidate.mqtt_host, "tls");
    candidate.mqtt_port = 19688;
    candidate.mqtt_tls_enabled = 1;
    assert(MqttTrialManager_StagePersistent(&candidate, 30, 300));
    assert(saves == 0 && strcmp(identity, "") == 0);
    to_verifying(300);
    assert(strcmp(identity, "NEW") == 0 && saves == 0);
    active = tls;
    MqttTrialManager_OnHeartbeatProof(++pub_sequence, 304);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_COMMITTING && saves == 0);
    MqttTrialManager_Poll(305);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_COMMITTED);
    assert(saves == 1 && strcmp(persisted.vid, "NEW") == 0 && persisted.mqtt_tls_enabled == 1);
    assert(strcmp(persisted.diag_log_host, "diag.example") == 0 &&
           strcmp(persisted.ota_origin, "https://ota.example") == 0 &&
           persisted.steering_center_count == 12345);
    MqttTrialManager_Init();
    candidate = persisted;
    strcpy(candidate.vid, "FAIL");
    strcpy(candidate.mqtt_host, "plain");
    candidate.mqtt_tls_enabled = 0;
    assert(MqttTrialManager_StagePersistent(&candidate, 31, 400));
    to_verifying(400);
    active = plain;
    MqttTrialManager_OnHeartbeatProof(++pub_sequence, 404);
    fail_save = 1;
    MqttTrialManager_Poll(405);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_RESTORING);
    assert(strcmp(identity, "NEW") == 0 && strcmp(persisted.vid, "NEW") == 0);
    puts("MQTT trial manager: real phases, candidate proof, cancel restore passed");
    return 0;
}
