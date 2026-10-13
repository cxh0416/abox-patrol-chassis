#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt_trial_manager.h"
#include "mqtt_trial_persistent.h"
#include "mqtt_runtime.h"
#include "mqtt_core.h"

static ABoxMqttConfig active = {"plain", 19683, "u", "p", 0, 0, {0}};
static ABoxMqttConfig staged;
static uint32_t pub_sequence;
static unsigned activations, restores;
static unsigned saves;
static unsigned stages;
static unsigned fail_save;
static unsigned fail_restore;
static MqttRuntimeState runtime_state = ABOX_MQTT_RUNTIME_READY;
static int first_stop_result;
static PatrolRemoteConfigV5_t persisted;
static PatrolRemoteConfigV5_t flash_config;
static ABoxTlsCredentials persisted_credentials, flash_credentials;
const ABoxTlsCredentials *PatrolConfig_MqttCredentials(void) { return &persisted_credentials; }
void PatrolConfig_Load(void) { persisted=flash_config; persisted_credentials=flash_credentials; }
uint8_t PatrolConfig_WriteMqttSnapshot(const PatrolRemoteConfigV5_t *c, const ABoxTlsCredentials *credentials)
{ saves++; if (fail_save) { fail_save=0; return 0; } flash_config=*c; flash_credentials=*credentials; return 1; }
static char identity[32];

const PatrolRemoteConfigV5_t *PatrolConfig_Get(void) { return &persisted; }
uint8_t PatrolConfig_IsCloudReady(const PatrolRemoteConfigV5_t *c) { return c && c->vid[0] && c->mqtt_host[0]; }
uint8_t PatrolConfig_IsVidValid(const char *vid)
{
    if (!vid || !vid[0]) return 0;
    unsigned i;
    for (i = 0; i < 32 && vid[i]; ++i) {
        unsigned char c = (unsigned char)vid[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return i < 32;
}
uint8_t PatrolMqttConfig_FromV5(const PatrolRemoteConfigV5_t *c, ABoxMqttConfig *out)
{
    if (!c || !out) return 0;
    *out = (ABoxMqttConfig){c->mqtt_host,c->mqtt_port,c->mqtt_username,c->mqtt_password,
                            c->mqtt_tls_enabled,c->mqtt_tls_profile_id,{0}};
    out->tls_credentials = persisted_credentials;
    return 1;
}
uint8_t PatrolConfig_Save(const PatrolRemoteConfigV5_t *c)
{ saves++; if (fail_save) { fail_save=0; return 0; } persisted=*c; return 1; }
uint8_t CloudService_ApplyMqttIdentity(const char *vid)
{ strcpy(identity, vid); return 1; }

int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *c)
{ stages++; staged = *c; return 1; }
int MqttRuntime_ActivateStagedCandidate(uint32_t now)
{ (void)now; activations++; return 1; }
int MqttRuntime_StopFirst(void) { return first_stop_result; }
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *c, uint32_t now)
{ (void)now; active = *c; restores++; return !fail_restore; }
int MqttRuntime_IsReady(void) { return 1; }
MqttRuntimeState MqttRuntime_State(void) { return runtime_state; }
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void) { return &active; }
const ABoxMqttConfig *MqttRuntime_StagedConfig(void) { return &staged; }
void MqttRuntime_BlockForRestoreFailure(void) { assert(0); }
uint8_t MQTT_Core_IsReady(void) { return 1; }
uint32_t MQTT_Core_GetPubOkSeq(void) { return pub_sequence; }

static void to_verifying(uint32_t now)
{
    MqttTrialManager_Accepted(now);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_PREPARING);
    MqttTrialManager_Poll(now);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_PREPARED, now+1);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_CONNECTING);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_CONNECTED, now+2);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_SUBSCRIBING);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_READY, now+3);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_VERIFYING);
}
int main(void)
{
    ABoxMqttConfig tls = {"tls", 19688, "u", "p", 1, 0, {0}};
    ABoxMqttConfig plain = {"plain", 19683, "u", "p", 0, 0, {0}};
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
    MqttTrialManager_Poll(204);
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
    PatrolRemoteConfigV5_t bad_candidate = candidate;
    strcpy(bad_candidate.vid, "bad.id");
    unsigned before_stages = stages, before_saves = saves;
    assert(!MqttTrialManager_StagePersistent(&bad_candidate, 30, 300));
    memset(bad_candidate.vid, 'a', sizeof(bad_candidate.vid));
    assert(!MqttTrialManager_StagePersistent(&bad_candidate, 30, 300));
    assert(stages == before_stages && saves == before_saves);
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
    MqttTrialManager_Poll(406);
    assert(strcmp(identity, "NEW") == 0 && strcmp(persisted.vid, "NEW") == 0);
    MqttTrialManager_Init();
    first_stop_result = 0;
    const char *invalid_vid[] = {"", "bad.id", "bad/id", "bad id",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (unsigned i = 0; i < sizeof(invalid_vid) / sizeof(invalid_vid[0]); ++i) {
        unsigned before_stages = stages, before_saves = saves;
        ABoxMqttConfig before_staged = staged;
        PatrolRemoteConfigV5_t before_persisted = persisted;
        char before_identity[sizeof(identity)]; memcpy(before_identity, identity, sizeof(identity));
        assert(!MqttTrialManager_StartEnrollment(&plain, invalid_vid[i], 500));
        assert(stages == before_stages && saves == before_saves);
        assert(!memcmp(&before_staged, &staged, sizeof(staged)));
        assert(!memcmp(&before_persisted, &persisted, sizeof(persisted)));
        assert(!memcmp(before_identity, identity, sizeof(identity)));
    }
    assert(MqttTrialManager_StartEnrollment(&plain, "PATROL-NEW", 500));
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_PREPARING);
    MqttTrialManager_Poll(500);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_PREPARED, 501);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_CONNECTED, 502);
    MqttTrialManager_OnRuntimeState(ABOX_MQTT_RUNTIME_READY, 503);
    MqttTrialManager_OnHeartbeatProof(++pub_sequence, 504);
    MqttTrialManager_Poll(505);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_COMMITTED);
    assert(strcmp(persisted.vid, "PATROL-NEW") == 0 && persisted.reserved0 == 1U);
    MqttTrialManager_Init();
    assert(MqttTrialManager_StartEnrollment(&plain, "PATROL-FAIL", 600));
    MqttTrialManager_Cancel(601);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_RESTORING);
    first_stop_result = 1;
    MqttTrialManager_Poll(602);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_FAILED);
    assert(strcmp(persisted.vid, "PATROL-NEW") == 0);
    MqttTrialManager_Init();
    active = plain;
    candidate = persisted;
    strcpy(candidate.mqtt_host, "broker.invalid");
    candidate.mqtt_tls_enabled = 1U;
    assert(MqttTrialManager_StagePersistent(&candidate, 32, 700));
    fail_restore = 1U;
    MqttTrialManager_Accepted(701);
    runtime_state = ABOX_MQTT_RUNTIME_BLOCKED;
    MqttTrialManager_Poll(702);
    MqttTrialManager_Poll(703);
    assert(MqttTrialManager_State() == ABOX_MQTT_TRIAL_RESTORE_FAILED);
    assert(strcmp(persisted.vid, "PATROL-NEW") == 0);
    /* Secret and endpoint form one trial snapshot, including save recovery. */
    fail_restore=0; runtime_state=ABOX_MQTT_RUNTIME_READY; active=plain;
    ABoxTlsCredentials key={0}; key.mode=ABOX_TLS_AUTH_PSK; key.generation=1;
    strcpy(key.identity,"host-test.g1"); strcpy(key.secret,"0123456789abcdefghijklmnopqrstuv");
    candidate=persisted; candidate.mqtt_tls_enabled=1;
    MqttTrialManager_Init();
    assert(MqttTrialManager_StagePersistentPsk(&candidate,&key,40,800));
    to_verifying(800); MqttTrialManager_OnHeartbeatProof(++pub_sequence,804);
    MqttTrialManager_Poll(805);
    assert(MqttTrialManager_State()==ABOX_MQTT_TRIAL_COMMITTED);
    assert(persisted_credentials.generation==1 && !memcmp(&persisted_credentials,&key,sizeof(key)));
    MqttTrialManager_Init(); key.generation=2; strcpy(key.identity,"host-test.g2");
    assert(MqttTrialManager_StagePersistentPsk(&candidate,&key,41,900));
    to_verifying(900); MqttTrialManager_OnHeartbeatProof(++pub_sequence,904);
    fail_save=1; MqttTrialManager_Poll(905);
    assert(MqttTrialManager_State()==ABOX_MQTT_TRIAL_RESTORING);
    assert(persisted_credentials.generation==1 && flash_credentials.generation==1);
    puts("MQTT trial manager: real phases, secret snapshot and failed-save recovery passed");
    return 0;
}
