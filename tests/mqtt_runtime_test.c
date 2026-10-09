#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "abox_ec800_at.h"
#include "mqtt_runtime.h"
#include "mqtt_core.h"

static uint32_t now_ms;
static uint8_t provisioned = 1U, time_valid, paused, security_ready, core_ready, core_auto_ready;
static unsigned revoked, stops;
static ABoxMqttConfig applied;
static char commands[32][160];
static unsigned command_count;
static int fail_tls_command;
static ABoxMqttRuntimeState observed[32];
static unsigned observed_count;

uint8_t MQTT_Core_ApplyRuntimeConfig(const ABoxMqttConfig *config)
{ applied = *config; return 1U; }
void MQTT_Core_SetPaused(uint8_t value) { paused = value; if (value) core_ready = 0U; }
void MQTT_Core_SetSecurityReady(uint8_t value) { security_ready = value; }
void MQTT_Core_Task(void) { if (core_auto_ready) core_ready = 1U; }
void MQTT_Core_Reset(void) { core_ready = 0U; }
int MQTT_Core_Stop(uint32_t now) { (void)now; ++stops; return 1; }
uint8_t MQTT_Core_IsReady(void) { return core_ready; }
uint8_t MQTT_Core_IsConnected(void) { return !paused && security_ready; }
MQTT_CoreState_t MQTT_Core_GetState(void) { return core_ready ? MQTT_CORE_ST_READY : MQTT_CORE_ST_SUB; }
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState state, uint32_t now)
{ (void)now; if (observed_count < 32U) observed[observed_count++] = state; }
uint8_t OtaService_IsProvisioned(void) { return provisioned; }
uint8_t OtaService_IsBusy(void) { return 0U; }
void RemoteService_RevokeForNetworkChange(void) { revoked++; }
void CloudTx_NetworkChanged(void) { }
void TimeBase_UpdateFromCCLK(const char *line)
{ if (strstr(line, "+CCLK:")) time_valid = 1U; }
uint8_t TimeBase_IsValid(void) { return time_valid; }

static uint32_t tick(void *context) { (void)context; return now_ms; }
static int write_at(void *context, const uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)context; (void)timeout;
    assert(command_count < 32U && length < sizeof(commands[0]));
    memcpy(commands[command_count], data, length);
    commands[command_count][length] = '\0';
    command_count++;
    return 1;
}
static int has_command(const char *part)
{
    unsigned i;
    for (i = 0U; i < command_count; ++i) if (strstr(commands[i], part)) return 1;
    return 0;
}
static void service(ABoxEc800At *at)
{
    unsigned handled = 0U;
    while (MqttRuntime_State() != MQTT_RUNTIME_READY &&
           MqttRuntime_State() != MQTT_RUNTIME_FAILED && handled++ < 10000U) {
        MqttRuntime_Task(now_ms);
        ABoxEc800At_Task(at);
        if (at->active_valid) {
            if (strstr(at->active.command, "AT+QMTDISC=")) {
                const char *result = "+QMTDISC: 0,0\r\n";
                ABoxEc800At_Feed(at, (const uint8_t *)result, (uint16_t)strlen(result));
            } else if (strstr(at->active.command, "AT+QMTCLOSE=")) {
                const char *result = "+QMTCLOSE: 0,0\r\n";
                ABoxEc800At_Feed(at, (const uint8_t *)result, (uint16_t)strlen(result));
            } else if (strstr(at->active.command, "AT+CCLK?")) {
                const char *clock_line = "+CCLK: \"26/09/22,10:00:00+32\"\r\n";
                ABoxEc800At_Feed(at, (const uint8_t *)clock_line, (uint16_t)strlen(clock_line));
                ABoxEc800At_Feed(at, (const uint8_t *)"OK\r\n", 4U);
            } else if (fail_tls_command && strstr(at->active.command, "QSSLCFG"))
                ABoxEc800At_Feed(at, (const uint8_t *)"ERROR\r\n", 7U);
            else
                ABoxEc800At_Feed(at, (const uint8_t *)"OK\r\n", 4U);
        }
        now_ms += 10U;
    }
}
static void reset_fakes(void)
{
    now_ms = 1U; time_valid = paused = security_ready = core_ready = 0U; core_auto_ready = 1U; revoked = 0U;
    command_count = stops = 0U; fail_tls_command = 0; memset(&applied, 0, sizeof(applied));
    observed_count = 0U;
    memset(commands, 0, sizeof(commands));
}
static void init_at(ABoxEc800At *at)
{
    ABoxEc800AtPort port = {0};
    port.tick_ms = tick; port.write = write_at;
    assert(ABoxEc800At_Init(at, &port));
}
int main(void)
{
    ABoxEc800At at;
    ABoxMqttConfig plain = {"plain.example", 19683U, "plain-user", "plain-pass", 0U, 0U, {0}};
    ABoxMqttConfig tls = {"tls.example", 8883U, "tls-user", "tls-pass", 1U, 0U, {0}};

    reset_fakes(); init_at(&at);
    assert(MqttRuntime_Init(&at, &plain, now_ms));
    assert(!MqttRuntime_IsReady());
    service(&at);
    assert(MqttRuntime_IsReady() && MqttRuntime_ActiveClient() == 0U && !MqttRuntime_ActiveTls());
    assert(observed_count >= 4U && observed[0] == MQTT_RUNTIME_PREPARED);
    assert(observed[1] == MQTT_RUNTIME_CONNECT && observed[2] == MQTT_RUNTIME_CONNECTED);
    assert(observed[3] == MQTT_RUNTIME_READY);
    assert(command_count == 0U && applied.tls_enabled == 0U && security_ready && !paused);

    assert(MqttRuntime_ApplyRamCandidate(&tls, now_ms));
    assert(revoked == 1U && !security_ready && paused);
    service(&at);
    if (!MqttRuntime_IsReady()) {
        unsigned i;
        fprintf(stderr, "state=%u commands=%u\n", (unsigned)MqttRuntime_State(), command_count);
        for (i = 0U; i < command_count; ++i) fprintf(stderr, "%s", commands[i]);
    }
    assert(MqttRuntime_IsReady() && MqttRuntime_ActiveClient() == 1U);
    assert(MqttRuntime_ActiveContext() == 2U && applied.tls_enabled == 1U);
    assert(stops > 0U && !has_command("QMTDISC") && !has_command("QMTCLOSE"));
    assert(has_command("QSSLCFG=\"seclevel\",2,1"));
    assert(has_command("QSSLCFG=\"ignorelocaltime\",2,0"));
    assert(has_command("QSSLCFG=\"cacert\",2,\"UFS:ota_ca.pem\""));
    assert(has_command("QMTCFG=\"SSL\",1,1,2"));

    /* A cancel/subscribe timeout can restore while the candidate is connected
       but its subscriptions have not completed. */
    core_auto_ready = 0U;
    assert(MqttRuntime_ApplyRamCandidate(&plain, now_ms));
    while (MqttRuntime_State() != MQTT_RUNTIME_CONNECTED && now_ms < 120000U) {
        MqttRuntime_Task(now_ms);
        ABoxEc800At_Task(&at);
        if (at.active_valid) {
            const char *reply = strstr(at.active.command, "AT+QMTDISC=") ? "+QMTDISC: 1,0\r\n" :
                                strstr(at.active.command, "AT+QMTCLOSE=") ? "+QMTCLOSE: 1,0\r\n" : "OK\r\n";
            ABoxEc800At_Feed(&at, (const uint8_t *)reply, (uint16_t)strlen(reply));
        }
        now_ms += 10U;
    }
    assert(MqttRuntime_State() == MQTT_RUNTIME_CONNECTED);
    core_auto_ready = 1U;
    assert(MqttRuntime_RestoreRamActive(&tls, now_ms));
    service(&at);
    assert(MqttRuntime_IsReady() && MqttRuntime_ActiveTls());

    reset_fakes(); init_at(&at); fail_tls_command = 1;
    assert(MqttRuntime_Init(&at, &tls, now_ms));
    service(&at);
    assert(MqttRuntime_State() == MQTT_RUNTIME_FAILED && !MqttRuntime_IsReady());
    assert(!security_ready && paused && !has_command("QMTOPEN=0") && !has_command("QMTCONN=0"));

    reset_fakes(); init_at(&at); core_auto_ready = 0U;
    assert(MqttRuntime_Init(&at, &tls, now_ms));
    service(&at);
    assert(MqttRuntime_State() == MQTT_RUNTIME_FAILED && !MqttRuntime_IsReady());
    assert(!security_ready && paused && !has_command("QMTOPEN=0") && !has_command("QMTCONN=0"));

    puts("MQTT runtime: plain client0, TLS client1/context2/CA, RAM switch revoke, no fallback passed");
    return 0;
}
