#include <stdio.h>
#include <string.h>

#include "absolute_encoder.h"
#include "chassis_cloud_protocol.h"
#include "chassis_control.h"
#include "chassis_power.h"
#include "fixed_point.h"
#include "patrol_canopen.h"
#include "patrol_diagnostics.h"
#include "steering_angle.h"
#include "vehicle_power_output.h"
#include "cJSON.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static uint8_t g_power_level;
static uint8_t test_power_write(uint8_t on)
{
    g_power_level = on ? 1U : 0U;
    return 1U;
}

static int test_sdo_waits_for_node(void)
{
    PatrolDiagnostics d;
    PatrolCanFrame f = {0}, request;
    PatrolDiagnostics_Init(&d);
    for (uint32_t now = 0; now < 10000; now += 10)
        CHECK(!PatrolDiagnostics_TakeSdoRead(&d, now, &request));
    CHECK(d.sdo_timeout_count == 0 && !d.pending_valid);
    f.id = 0x702; f.dlc = 1; f.data[0] = 0;
    PatrolDiagnostics_OnCanFrame(&d, &f, 10000);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 10001, &request));
    f.data[0] = 4; /* NMT Stopped does not provide an SDO service. */
    PatrolDiagnostics_OnCanFrame(&d, &f, 10002);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 10003, &request));
    f.data[0] = 0x7F;
    PatrolDiagnostics_OnCanFrame(&d, &f, 10004);
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 10005, &request));
    CHECK(request.id == 0x602 && request.data[1] == 0x41 && request.data[2] == 0x60);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 14000, &request));
    CHECK(!d.pending_valid && d.sdo_timeout_count == 0);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 14010, &request));
    f.id = 0x701; f.data[0] = 5;
    PatrolDiagnostics_OnCanFrame(&d, &f, 14011);
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 14012, &request));
    CHECK(request.id == 0x601);
    return 0;
}

static int test_field_sdo_replies(void)
{
    /* VSY field log 2026-08-27: 6041 / 2000..2005 omit CAN padding.
     * The response width is carried by the SDO command, not DLC == 8. */
    static const uint8_t reply_data[12][8] = {
        {0x4B,0x41,0x60,0,0x50,0x20}, {0x43,0x64,0x60,0,0x29,0x15,0,0},
        {0x4B,0x41,0x60,0,0x50,0x20}, {0x43,0x69,0x60,0,0,0,0,0},
        {0x43,0x69,0x68,0,0,0,0,0}, {0x4B,0x00,0x20,0,0,0},
        {0x4B,0x01,0x20,0,0,0}, {0x4B,0x02,0x20,0,5,0},
        {0x4B,0x03,0x20,0,4,0}, {0x4B,0x04,0x20,0,0x22,0},
        {0x4B,0x05,0x20,0,0xD8,0xFF}, {0x80,0x06,0x20,0,0,0,2,6}
    };
    static const uint8_t lengths[12] = {6,8,6,8,8,6,6,6,6,6,6,8};
    PatrolDiagnostics d;
    PatrolDiagnosticsSnapshot s;
    PatrolCanFrame request, response = {0};
    int32_t value;
    uint8_t size;
    char json[900], envelope[1100], tiny[8];
    cJSON* parsed;
    PatrolDiagnostics_Init(&d);
    response.id = 0x701; response.dlc = 1; response.data[0] = 0x7F;
    PatrolDiagnostics_OnCanFrame(&d, &response, 1);
    response.id = 0x702;
    PatrolDiagnostics_OnCanFrame(&d, &response, 1);
    for (uint8_t i = 0; i < 12; i++) {
        CHECK(PatrolDiagnostics_TakeSdoRead(&d, 10 + 5 * i, &request));
        response.id = request.id - 0x80;
        response.dlc = lengths[i];
        memcpy(response.data, reply_data[i], 8);
        /* One missing value byte must not complete the request, including Abort. */
        response.dlc--;
        PatrolDiagnostics_OnCanFrame(&d, &response, 11 + 5 * i);
        CHECK(d.pending_valid);
        response.dlc++;
        PatrolDiagnostics_OnCanFrame(&d, &response, 12 + 5 * i);
        CHECK(!d.pending_valid);
    }
    PatrolDiagnostics_Snapshot(&d, 75, &s);
    CHECK(s.steering_online && s.drive_online);
    CHECK(s.steering_feedback_fresh && s.drive_feedback_fresh);
    CHECK(!s.encoder_online && s.coherent_feedback_ms == 0);
    CHECK(s.steering_position_count == 5417 && s.left_actual_rpm == 0 && s.right_actual_rpm == 0);
    CHECK(s.steering_status_word == 0x2050 && s.drive_status_word == 0x2050);
    CHECK(s.sdo_success_count == 11 && s.sdo_success_mask == 0x7FF);
    CHECK(s.sdo_timeout_count == 0 && s.sdo_abort_count == 1 && s.sdo_abort_mask == 0x800);
    CHECK(s.last_sdo_abort_code == 0x06020000 && s.can_ignored_count == 12);
    CHECK(s.drive_objects[2] == 5 && s.drive_objects[4] == 34);
    PatrolDiagnostics_Snapshot(&d, 400, &s);
    CHECK(!s.steering_feedback_fresh && !s.drive_feedback_fresh);
    /* Accept 1/2/3/4 declared bytes, with or without trailing padding. */
    response.id = 0x581; response.data[1] = 0x41; response.data[2] = 0x60; response.data[3] = 0;
    for (uint8_t width = 1; width <= 4; width++) {
        response.data[0] = (uint8_t)(0x43 + (4 - width) * 4);
        response.dlc = 4 + width;
        CHECK(PatrolCanopen_ParseSdoValue(&response, 1, 0x6041, 0, &value, &size));
        CHECK(size == width);
        response.dlc--;
        CHECK(!PatrolCanopen_ParseSdoValue(&response, 1, 0x6041, 0, &value, &size));
        response.dlc = 8;
        CHECK(PatrolCanopen_ParseSdoValue(&response, 1, 0x6041, 0, &value, &size));
    }
    response.dlc = 9;
    CHECK(!PatrolCanopen_ParseSdoValue(&response, 1, 0x6041, 0, &value, &size));
    /* Worst-width counters and signed feedback must fit the existing response budget. */
    memset(&s, 0xFF, sizeof(s));
    s.steering_position_count = s.left_actual_rpm = s.right_actual_rpm = INT32_MIN;
    for (uint8_t i = 0; i < 7; i++) s.drive_objects[i] = INT32_MIN;
    CHECK(PatrolDiagnostics_FormatCanJson(&s, 0, 0, UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                          json, sizeof(json)));
    CHECK(!PatrolDiagnostics_FormatCanJson(&s, 0, 0, 0, 0, 0, tiny, sizeof(tiny)));
    snprintf(envelope, sizeof(envelope), "{\"version\":\"2.0\",\"timestamp\":18446744073709551615,"
        "\"data\":{\"product\":\"patrol_vehicle_chassis\",\"requestId\":\"%063d\",\"code\":200,"
        "\"msg\":\"OK\",\"result\":%s}}", 0, json);
    CHECK(strlen(envelope) < 1080);
    parsed = cJSON_Parse(envelope); CHECK(parsed != NULL); cJSON_Delete(parsed);
    return 0;
}

int main(void)
{
    CHECK(test_sdo_waits_for_node() == 0);
    CHECK(test_field_sdo_replies() == 0);
    ChassisControl control;
    ChassisFeedback feedback = {0};
    PatrolCanFrame frame;
    ChassisPower power;
    uint8_t request[8];
    uint8_t response[9] = {1, 3, 4, 0x12, 0x34, 0x56, 0x78, 0, 0};
    uint16_t crc;
    uint32_t count;
    uint16_t config_values[3];
    uint8_t config_response[11] = {1, 3, 6, 0x00, 0x01, 0x00, 0x03, 0x4B, 0x00, 0, 0};
    PatrolDiagnostics diagnostics;
    PatrolDiagnosticsSnapshot diagnostic_snapshot;
    char fixed[24];
    ChassisCloudCommand command;
    const char json[] = "{\"version\":\"3.0\",\"data\":{\"requestId\":\"r1\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"acquire\",\"sessionId\":\"s1\",\"seq\":1,\"leaseMs\":1000,\"driveRpm\":50,\"steeringDeg\":1}}}";
    const char wrong_type[] = "{\"version\":\"3.0\",\"data\":{\"requestId\":\"r2\",\"deviceType\":\"wing_actuator\",\"command\":\"chassis_control\",\"params\":{}}}";

    CHECK(SteeringAngle_FromControllerCount(-58015, -85123, 2710800U, 1) == 10000);
    CHECK(SteeringAngle_FromControllerCount(-112231, -85123, 2710800U, 1) == -10000);
    CHECK(SteeringAngle_FromAbsoluteCount(3734U, 3182U, 55250U, 1) == 9990);
    CHECK(SteeringAngle_FromAbsoluteCount(2629U, 3182U, 55250U, 1) == -10009);
    CHECK(SteeringAngle_FromAbsoluteCount(2629U, 3182U, 55250U, -1) == 10009);

    frame = PatrolCanopen_SdoWrite16(2, 0x6040, 0, 0x000F);
    CHECK(frame.id == 0x602 && frame.data[0] == 0x2B && frame.data[1] == 0x40 && frame.data[2] == 0x60);
    frame = PatrolCanopen_SdoWrite32(2, 0x60FF, 0, -300);
    CHECK(frame.data[4] == 0xD4 && frame.data[7] == 0xFF);

    AbsoluteEncoder_BuildReadRequest(request);
    CHECK(request[0] == 1 && request[1] == 3 && request[3] == 3 && request[5] == 2);
    crc = AbsoluteEncoder_Crc16(response, 7);
    response[7] = (uint8_t)crc; response[8] = (uint8_t)(crc >> 8);
    CHECK(AbsoluteEncoder_ParseResponse(response, sizeof(response), &count));
    CHECK(count == 0x56781234U);
    crc = AbsoluteEncoder_Crc16(config_response, 9);
    config_response[9] = (uint8_t)crc; config_response[10] = (uint8_t)(crc >> 8);
    CHECK(AbsoluteEncoder_ParseHoldingResponse(config_response, sizeof(config_response), config_values, 3U));
    CHECK(config_values[0] == 1U && config_values[1] == 3U && config_values[2] == 0x4B00U);

    CHECK(FixedPoint_FormatMilli(fixed, sizeof(fixed), 0) && strcmp(fixed, "0.000") == 0);
    CHECK(FixedPoint_FormatMilli(fixed, sizeof(fixed), -1) && strcmp(fixed, "-0.001") == 0);
    CHECK(FixedPoint_FormatMilli(fixed, sizeof(fixed), 12345) && strcmp(fixed, "12.345") == 0);

    PatrolDiagnostics_Init(&diagnostics);
    memset(&frame, 0, sizeof(frame));
    frame.id = 0x701U; frame.dlc = 1U; frame.data[0] = 5U;
    PatrolDiagnostics_OnCanFrame(&diagnostics, &frame, 100U);
    frame.id = 0x702U;
    PatrolDiagnostics_OnCanFrame(&diagnostics, &frame, 100U);
    for (uint8_t sample = 0U; sample < 5U; sample++)
    {
        PatrolCanFrame response_frame;
        int32_t value = sample == 1U ? 1000 : (sample == 3U ? -12 : (sample == 4U ? 15 : 0));
        CHECK(PatrolDiagnostics_TakeSdoRead(&diagnostics, 110U + sample, &frame));
        CHECK((frame.id == 0x601U || frame.id == 0x602U) && frame.data[0] == 0x40U);
        memset(&response_frame, 0, sizeof(response_frame));
        response_frame.id = (uint16_t)(frame.id - 0x80U);
        response_frame.dlc = 8U;
        response_frame.data[0] = 0x43U;
        response_frame.data[1] = frame.data[1];
        response_frame.data[2] = frame.data[2];
        response_frame.data[3] = frame.data[3];
        response_frame.data[4] = (uint8_t)value;
        response_frame.data[5] = (uint8_t)((uint32_t)value >> 8);
        response_frame.data[6] = (uint8_t)((uint32_t)value >> 16);
        response_frame.data[7] = (uint8_t)((uint32_t)value >> 24);
        PatrolDiagnostics_OnCanFrame(&diagnostics, &response_frame, 120U + sample);
    }
    PatrolDiagnostics_OnEncoderConfig(&diagnostics, config_values, 125U);
    PatrolDiagnostics_OnEncoderCount(&diagnostics, 123456U, 125U);
    PatrolDiagnostics_Snapshot(&diagnostics, 130U, &diagnostic_snapshot);
    CHECK(diagnostic_snapshot.steering_online && diagnostic_snapshot.drive_online);
    CHECK(diagnostic_snapshot.steering_feedback_fresh && diagnostic_snapshot.drive_feedback_fresh);
    CHECK(diagnostic_snapshot.encoder_online && diagnostic_snapshot.encoder_config_valid);
    CHECK(diagnostic_snapshot.steering_position_count == 1000);
    CHECK(diagnostic_snapshot.left_actual_rpm == -12 && diagnostic_snapshot.right_actual_rpm == 15);
    CHECK(diagnostic_snapshot.encoder_count == 123456U);
    CHECK(PatrolDiagnostics_TakeSdoRead(&diagnostics, 200U, &frame));
    CHECK(!PatrolDiagnostics_TakeSdoRead(&diagnostics, 250U, &frame));
    CHECK(PatrolDiagnostics_TakeSdoRead(&diagnostics, 260U, &frame));
    CHECK(frame.data[0] == 0x40U);

    ChassisControl_Init(&control, 1);
    feedback.calibrated = 1; feedback.steering_online = 1; feedback.drive_online = 1;
    feedback.power_ready = 1; feedback.feedback_ms = 100;
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "acquire", "session", 1, 1000, 150, 3000, 100));
    CHECK(control.target_rpm == 150 && control.target_steering_mdeg == 3000);
    CHECK(ChassisControl_Apply(&control, "renew", "session", 2, 1000, -50, -1000, 200));
    CHECK(control.target_rpm == 0 && control.reversal_pending);
    feedback.feedback_ms = 220; ChassisControl_SetFeedback(&control, &feedback); ChassisControl_Tick(&control, 220);
    ChassisControl_Tick(&control, 221); ChassisControl_Tick(&control, 222);
    CHECK(control.target_rpm == 0 && control.zero_confirmations == 1U);
    feedback.feedback_ms = 230; ChassisControl_SetFeedback(&control, &feedback); ChassisControl_Tick(&control, 230);
    feedback.feedback_ms = 240; ChassisControl_SetFeedback(&control, &feedback); ChassisControl_Tick(&control, 240);
    CHECK(control.target_rpm == -50 && !control.reversal_pending);
    feedback.feedback_ms = 1300; ChassisControl_SetFeedback(&control, &feedback);
    ChassisControl_Tick(&control, 1300);
    CHECK(control.target_rpm == 0 && control.stop_reason == CHASSIS_STOP_LEASE_EXPIRED);

    ChassisControl_Init(&control, 1);
    feedback.feedback_ms = 0; feedback.left_actual_rpm = 50; feedback.right_actual_rpm = 50;
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "acquire", "reverse", 1, 1000, 50, 0, 0));
    CHECK(ChassisControl_Apply(&control, "renew", "reverse", 2, 1000, -50, 0, 10));
    feedback.feedback_ms = 900; ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "renew", "reverse", 3, 1000, -50, 0, 900));
    feedback.feedback_ms = 1800; ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "renew", "reverse", 4, 1000, -50, 0, 1800));
    feedback.feedback_ms = 2010; ChassisControl_SetFeedback(&control, &feedback);
    ChassisControl_Tick(&control, 2010);
    CHECK(control.fault_latched && control.stop_reason == CHASSIS_STOP_DIRECTION_TIMEOUT);
    CHECK(!ChassisControl_Apply(&control, "acquire", "again", 1, 1000, 50, 0, 2010));

    ChassisControl_Init(&control, 0);
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(!ChassisControl_Apply(&control, "acquire", "blocked", 1, 1000, 50, 0, 0));
    CHECK(control.target_rpm == 0);

    ChassisPower_Init(&power, 0, 0);
    CHECK(!ChassisPower_RequestOn(&power, 0) && power.relay_request == 0);
    ChassisPower_Init(&power, 1, 0);
    CHECK(ChassisPower_RequestOn(&power, 0));
    ChassisPower_Tick(&power, 10, 1, 1);
    CHECK(power.state == CHASSIS_POWER_READY && power.enable_request);
    ChassisPower_RequestOff(&power, 20);
    CHECK(!power.enable_request && power.state == CHASSIS_POWER_STOPPING);
    ChassisPower_Tick(&power, 30, 1, 1);
    ChassisPower_Tick(&power, 40, 0, 1);
    CHECK(power.state == CHASSIS_POWER_OFF && !power.relay_request);

    g_power_level = 1U;
    VehiclePowerOutput_Init(test_power_write);
    CHECK(VehiclePowerOutput_IsAvailable() && !VehiclePowerOutput_IsOn() && g_power_level == 0U);
    CHECK(VehiclePowerOutput_Set(1U) && VehiclePowerOutput_IsOn() && g_power_level == 1U);
    CHECK(VehiclePowerOutput_Set(0U) && !VehiclePowerOutput_IsOn() && g_power_level == 0U);

    CHECK(ChassisCloud_Parse(json, (uint16_t)strlen(json), &command) == CHASSIS_CLOUD_OK);
    CHECK(command.drive_rpm == 50 && command.steering_mdeg == 1000 && strcmp(command.session_id, "s1") == 0);
    CHECK(ChassisCloud_Parse(wrong_type, (uint16_t)strlen(wrong_type), &command) ==
          CHASSIS_CLOUD_DEVICE_TYPE_MISMATCH);
    return 0;
}
