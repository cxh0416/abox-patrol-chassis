#include "patrol_config.h"
#include "remote_service.h"
#include "mqtt_config_adapter.h"
#include "vehicle_power_output.h"
#include "time_base.h"
#include "chassis_cloud_protocol.h"
#include "abox_mqtt_v4.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static PatrolRemoteConfigV5_t flash;
static ABoxTlsCredentials flash_credentials;
static uint8_t valid, save_ok = 1, power_level;
static uint32_t tick = 1000;
static uint64_t utc_now = 1788838333000ULL;
static uint8_t time_trusted = 1U;
uint32_t HAL_GetTick(void) { return tick; }
ABoxClockSnapshot TimeBase_GetSnapshot(void) {
 ABoxClockSnapshot s={utc_now, tick, 0U, 1U, ABOX_CLOCK_PLATFORM, time_trusted ? ABOX_CLOCK_SYNCHRONIZED : ABOX_CLOCK_UNTRUSTED}; return s;
}
uint8_t TimeBase_FormatUtcMs(char* s, uint16_t n) { return (uint8_t)ABoxMqttV4_FormatInteger(s, n, utc_now); }
uint8_t OTA_Info_LoadRemoteConfig(PatrolRemoteConfigV5_t* c) { if (valid) *c = flash; return valid; }
uint8_t OTA_Info_SaveRemoteConfig(const PatrolRemoteConfigV5_t* c)
{ if (!save_ok) return 0; flash = *c; valid = 1; return 1; }
uint8_t OTA_Info_LoadMqttCredentials(ABoxTlsCredentials* c)
{ if (!c) return 0; *c = flash_credentials; return 1; }
uint8_t OTA_Info_SaveMqttSnapshot(const PatrolRemoteConfigV5_t* c, const ABoxTlsCredentials* credentials)
{
    if (!credentials || !OTA_Info_SaveRemoteConfig(c)) return 0;
    flash_credentials = *credentials;
    return 1;
}
static uint8_t write_power(uint8_t on) { power_level = on; return 1; }

static void command(const char* session, int rpm, int angle, const char* action, int seq,
                    unsigned expected_code)
{
    char json[512], response[512], code[24];
    snprintf(json, sizeof(json), "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r%d\","
        "\"command\":\"chassis_control\",\"params\":{\"action\":\"%s\","
        "\"sessionId\":\"%s\",\"seq\":%d,\"leaseMs\":1000,"
        "\"driveRpm\":%d,\"steeringDeg\":%d}}}", seq, action, session, seq, rpm, angle);
    assert(RemoteService_HandleControl(json, (uint16_t)strlen(json), response, sizeof(response)));
    snprintf(code, sizeof(code), "\"code\":%u", expected_code);
    assert(strstr(response, code) != NULL);
}

static void test_request_identity_and_boundaries(void)
{
    char payload[1026], response[512], cached[512], id[66];
    ChassisCloudCommand parsed;
    const char *first = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"collision-1\","
        "\"command\":\"chassis_control\",\"params\":{\"action\":\"acquire\",\"sessionId\":\"collision-session\","
        "\"seq\":134893414,\"leaseMs\":1000,\"driveRpm\":-362,\"steeringDeg\":-13}}}";
    const char *collision = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"collision-1\","
        "\"command\":\"chassis_control\",\"params\":{\"action\":\"acquire\",\"sessionId\":\"collision-session\","
        "\"seq\":2828726036,\"leaseMs\":1000,\"driveRpm\":442,\"steeringDeg\":26}}}";
    RemoteService_RevokeForNetworkChange();
    assert(RemoteService_HandleControl(first, (uint16_t)strlen(first), cached, sizeof(cached)));
    assert(strstr(cached, "\"code\":200"));
    /* Both normalized requests have FNV-1a 0x7a8cd4dc, but differ in parameters. */
    assert(RemoteService_HandleControl(collision, (uint16_t)strlen(collision), response, sizeof(response)));
    assert(strstr(response, "\"code\":4006") && RemoteService_State()->requested_rpm == -362 &&
           RemoteService_State()->last_seq == 134893414);
    const char *retry = "{\"timestamp\":1788838333001,\"version\":\"4.0\",\"data\":{\"params\":{"
        "\"steeringDeg\":-13.000,\"driveRpm\":-362,\"leaseMs\":1000,\"seq\":134893414,"
        "\"sessionId\":\"collision-session\",\"action\":\"acquire\"},\"command\":\"chassis_control\",\"requestId\":\"collision-1\"}}";
    assert(RemoteService_HandleControl(retry, (uint16_t)strlen(retry), response, sizeof(response)));
    assert(!strcmp(response, cached));
    assert(RemoteService_State()->lease_deadline_ms == tick + 1000);
    const char *stop = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"collision-1\","
        "\"command\":\"chassis_control\",\"params\":{\"action\":\"stop\",\"sessionId\":\"collision-session\","
        "\"seq\":134893415,\"leaseMs\":1000,\"driveRpm\":0,\"steeringDeg\":0}}}";
    assert(RemoteService_HandleControl(stop, (uint16_t)strlen(stop), response, sizeof(response)));
    assert(strstr(response, "\"code\":4006") && !RemoteService_State()->enable_request);
    for (unsigned n = 63; n <= 65; ++n) {
        memset(id, 'x', n); id[n] = '\0';
        snprintf(payload, sizeof(payload), "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{"
            "\"requestId\":\"%s\",\"command\":\"chassis_control\",\"params\":{\"action\":\"acquire\","
            "\"sessionId\":\"boundary\",\"seq\":4294967295,\"leaseMs\":1000,\"driveRpm\":500,\"steeringDeg\":-30}}}", id);
        assert(ChassisCloud_Parse(payload, (uint16_t)strlen(payload), &parsed) ==
            (n <= 64 ? CHASSIS_CLOUD_OK : CHASSIS_CLOUD_INVALID));
        if (n <= 64) {
            RemoteService_RevokeForNetworkChange();
            assert(RemoteService_HandleControl(payload, (uint16_t)strlen(payload), cached, sizeof(cached)));
            assert(strstr(cached, id) && strstr(cached, "\"code\":200"));
            assert(RemoteService_HandleControl(payload, (uint16_t)strlen(payload), response, sizeof(response)));
            assert(!strcmp(cached, response));
        }
    }
    for (unsigned n = 1023; n <= 1025; ++n) {
        size_t used = strlen(first);
        memcpy(payload, first, used); memset(payload + used, ' ', n - used); payload[n] = '\0';
        assert(ChassisCloud_Parse(payload, (uint16_t)n, &parsed) ==
            (n <= 1024 ? CHASSIS_CLOUD_OK : CHASSIS_CLOUD_INVALID));
        if (n == 1025) assert(parsed.request_id[0] == '\0');
    }
    /* Longest cached error: full ID, maximum UTC, maximum sequence and targets. */
    memset(id, 'y', 64); id[64] = '\0';
    snprintf(payload, sizeof(payload), "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{"
        "\"requestId\":\"%s\",\"command\":\"chassis_control\",\"params\":{\"action\":\"renew\","
        "\"sessionId\":\"boundary\",\"seq\":4294967295,\"leaseMs\":1000,\"driveRpm\":-500,\"steeringDeg\":-30}}}", id);
    uint64_t saved_utc = utc_now; utc_now = 9007199254740991ULL;
    assert(RemoteService_HandleControl(payload, (uint16_t)strlen(payload), cached, sizeof(cached)));
    assert(strstr(cached, "\"code\":4001") && strstr(cached, id));
    assert(strlen(cached) < 288);
    assert(RemoteService_HandleControl(payload, (uint16_t)strlen(payload), response, sizeof(response)));
    assert(!strcmp(cached, response));
    utc_now = saved_utc;
    RemoteService_RevokeForNetworkChange();
}

int main(void)
{
    ABoxMqttConfig mqtt_runtime;
    ChassisFeedback feedback = {0};
    char response[512];
    const char* power_on = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"power-1\","
        "\"command\":\"power_on\",\"expiresAt\":1788838338000,\"params\":null}}";
    VehiclePowerOutput_Init(write_power);
    PatrolConfig_Load();
    assert(!PatrolConfig_Get()->calibrated && PatrolConfig_CalibrationCompatible());
    save_ok = 0;
    assert(!PatrolConfig_SetCalibration(-85123, 0, 2710800, 55250, 1, 1, 2000));
    save_ok = 1;
    assert(PatrolConfig_SetCalibration(-85123, 0, 2710800, 55250, 1, 1, 2000));
    flash.reserved_remote = 1; /* Existing V5 byte remains readable after OTA. */
    PatrolConfig_Load();
    assert(PatrolConfig_Get()->calibrated && PatrolConfig_Get()->reserved_remote == 1);
    assert(PatrolConfig_SingleTurnProfileReady());
    assert(PatrolConfig_SetMqtt("PATROL-001", "broker.example", 19683, "device", "secret"));
    flash.mqtt_tls_enabled = 1; flash.mqtt_tls_profile_id = 0; PatrolConfig_Load();
    assert(PatrolMqttConfig_FromV5(PatrolConfig_Get(), &mqtt_runtime));
    assert(!strcmp(mqtt_runtime.host, flash.mqtt_host) && mqtt_runtime.port == flash.mqtt_port);
    RemoteService_Init();
    assert(RemoteService_HandleControl(power_on, (uint16_t)strlen(power_on), response, sizeof(response)));
    assert(power_level && strstr(response, "\"powerOutputOn\":true"));
    /* Expiry/quality do not change an accepted discrete request's replay. */
    utc_now += 6000; time_trusted = 0U;
    assert(VehiclePowerOutput_Set(0U));
    assert(RemoteService_HandleControl(power_on, (uint16_t)strlen(power_on), response, sizeof(response)));
    assert(!power_level && strstr(response, "\"code\":200") && strstr(response, "\"powerOutputOn\":true"));
    const char *fresh = "{\"version\":\"4.0\",\"timestamp\":1788838339000,\"data\":{"
        "\"requestId\":\"power-fresh\",\"command\":\"power_on\",\"params\":null,\"expiresAt\":1788838344000}}";
    assert(RemoteService_HandleControl(fresh, (uint16_t)strlen(fresh), response, sizeof(response)));
    assert(!power_level && strstr(response, "\"code\":4003"));
    time_trusted = 1U;
    const char *future = "{\"version\":\"4.0\",\"timestamp\":1788838339800,\"data\":{"
        "\"requestId\":\"power-future\",\"command\":\"power_on\",\"params\":null,\"expiresAt\":1788838344800}}";
    assert(RemoteService_HandleControl(future, (uint16_t)strlen(future), response, sizeof(response)));
    assert(power_level && strstr(response, "\"code\":200"));
    command("new-session", 0, 0, "acquire", 1, 400);
    feedback.calibrated = feedback.encoder_profile_ready = feedback.encoder_range_valid = 1;
    feedback.steering_online = feedback.drive_online = feedback.power_ready = 1;
    feedback.feedback_ms = tick;
    RemoteService_UpdateFeedback(&feedback);
    command("new-session", 200, 30, "acquire", 2, 200);
    assert(RemoteService_State()->session_active && RemoteService_State()->target_rpm == 200);
    command("wrong-session", 0, 0, "stop", 3, 200);
    assert(RemoteService_State()->session_active &&
           strcmp(RemoteService_State()->session_id, "new-session") == 0 &&
           RemoteService_State()->target_rpm == 0 && !RemoteService_State()->enable_request);
    command("wrong-session", 0, 0, "stop", 3, 200); /* Replayed stop remains effective. */
    RemoteService_RevokeForNetworkChange();
    assert(!RemoteService_State()->session_active && !RemoteService_State()->enable_request &&
           RemoteService_State()->target_rpm == 0 && RemoteService_State()->session_id[0] == '\0');
    command("new-session", 0, 0, "renew", 4, 4003);
    command("after-reconnect", 50, 0, "acquire", 5, 200);
    tick += 1001; feedback.feedback_ms = tick; RemoteService_UpdateFeedback(&feedback);
    RemoteService_SafetyTick(tick);
    assert(!RemoteService_State()->session_active && !RemoteService_State()->enable_request);
    command("after-reconnect", 50, 0, "renew", 6, 4003);
    command("final-session", 0, 0, "acquire", 7, 200);
    command("final-session", 0, 0, "stop", 8, 200);
    assert(!RemoteService_State()->session_active);
    test_request_identity_and_boundaries();
    RemoteService_ExecutionFault();
    command("fault-session", 0, 0, "acquire", 9, 400);
    assert(RemoteService_State()->fault_latched);
    puts("PASS Config V5 layout, uncalibrated gate, direct control session, reconnect and lease revoke");
    return 0;
}
