#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mqtt_trial_manager.h"
#include "mqtt_runtime.h"
#include "mqtt_core.h"

static ABoxMqttConfig active = {"plain", 19683, "u", "p", 0, 0};
static ABoxMqttConfig staged;
static uint32_t pub_sequence;
static unsigned activations, restores;

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
    puts("MQTT trial manager: real phases, candidate proof, cancel restore passed");
    return 0;
}
