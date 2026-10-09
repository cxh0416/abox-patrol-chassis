#include "patrol_config.h"
#include "remote_service.h"
#include "mqtt_config_adapter.h"
#include "vehicle_power_output.h"
#include "time_base.h"
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
uint8_t TimeBase_FormatUtcMs(char* s, uint16_t n) { snprintf(s, n, "1788838333000"); return 1; }
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
    RemoteService_ExecutionFault();
    command("fault-session", 0, 0, "acquire", 9, 400);
    assert(RemoteService_State()->fault_latched);
    puts("PASS Config V5 layout, uncalibrated gate, direct control session, reconnect and lease revoke");
    return 0;
}
