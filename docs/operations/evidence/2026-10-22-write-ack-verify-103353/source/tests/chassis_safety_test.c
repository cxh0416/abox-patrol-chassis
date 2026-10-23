#include <stdio.h>
#include <string.h>

#include "absolute_encoder.h"
#include "chassis_cloud_protocol.h"
#include "chassis_control.h"
#include "fixed_point.h"
#include "patrol_canopen.h"
#include "patrol_diagnostics.h"
#include "steering_angle.h"
#include "vehicle_power_output.h"
#include "cJSON.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static uint8_t g_power_level;
static int test_status_snapshot_age(void)
{
    PatrolDiagnostics d;
    PatrolDiagnosticsSnapshot s;
    PatrolDiagnostics_Init(&d);
    d.steering_status_word = d.drive_status_word = 0x21;
    d.steering_status_ms = 0xfffffff0U;
    d.drive_status_ms = 0xffffffe0U;
    PatrolDiagnostics_Snapshot(&d, 0x10, &s);
    CHECK(s.steering_status_fresh && s.drive_status_fresh);
    PatrolDiagnostics_Snapshot(&d, 0xe0, &s);
    CHECK(s.steering_status_fresh && !s.drive_status_fresh);
    PatrolDiagnostics_Snapshot(&d, 0xf0, &s);
    CHECK(!s.steering_status_fresh && !s.drive_status_fresh);
    return 0;
}
static uint8_t test_power_write(uint8_t on)
{
    g_power_level = on ? 1U : 0U;
    return 1U;
}

static int test_sdo_safety_priority(void)
{
    PatrolDiagnostics d;
    PatrolCanFrame f;
    PatrolDiagnostics_Init(&d);
    d.steering_heartbeat_seen = d.drive_heartbeat_seen = 1;
    d.steering_heartbeat_state = d.drive_heartbeat_state = 0x7f;
    d.steering_heartbeat_ms = d.drive_heartbeat_ms = 100;
    d.next_sdo_entry = 11; /* optional object must not precede due feedback */
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 100, &f, 1U));
    CHECK(d.pending_entry < 5);
    d.pending_valid = 0;
    for (unsigned i = 0; i < 5; ++i) d.sdo_due_ms[i] = 200;
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 101, &f, 1U));
    CHECK(d.pending_entry >= 5); /* low-rate reads retain spare capacity */
    d.pending_valid = 0;
    d.motion_priority = 1;
    for (unsigned i = 0; i < 7; ++i) d.sdo_due_ms[i] = 200;
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 102, &f, 1U));
    d.sdo_due_ms[5] = 100; /* drive fault registers are never excluded */
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 103, &f, 1U) && d.pending_entry == 5);
    return 0;
}

static int test_sdo_waits_for_node(void)
{
    PatrolDiagnostics d;
    PatrolCanFrame f = {0}, request;
    PatrolDiagnostics_Init(&d);
    for (uint32_t now = 0; now < 10000; now += 10)
        CHECK(!PatrolDiagnostics_TakeSdoRead(&d, now, &request, 1U));
    CHECK(d.sdo_timeout_count == 0 && !d.pending_valid);
    f.id = 0x702; f.dlc = 1; f.data[0] = 0;
    PatrolDiagnostics_OnCanFrame(&d, &f, 10000);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 10001, &request, 1U));
    f.data[0] = 4; /* NMT Stopped does not provide an SDO service. */
    PatrolDiagnostics_OnCanFrame(&d, &f, 10002);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 10003, &request, 1U));
    f.data[0] = 0x7F;
    PatrolDiagnostics_OnCanFrame(&d, &f, 10004);
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 10005, &request, 1U));
    CHECK(request.id == 0x602 && request.data[1] == 0x41 && request.data[2] == 0x60);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 14000, &request, 1U));
    CHECK(!d.pending_valid && d.sdo_timeout_count == 0);
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 14010, &request, 1U));
    f.id = 0x701; f.data[0] = 5;
    PatrolDiagnostics_OnCanFrame(&d, &f, 14011);
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 14012, &request, 1U));
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
        CHECK(PatrolDiagnostics_TakeSdoRead(&d, 10 + 5 * i, &request, 1U));
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

// 建立真实且新鲜的诊断采样起点；重试测试不能靠默认零值冒充状态或反馈有效。
static void retry_fixture(PatrolDiagnostics* d, uint32_t now)
{
    PatrolDiagnostics_Init(d);
    d->steering_heartbeat_seen = d->drive_heartbeat_seen = 1U;
    d->steering_heartbeat_state = d->drive_heartbeat_state = 5U;
    d->steering_heartbeat_ms = d->drive_heartbeat_ms = now;
    d->steering_status_word = d->drive_status_word = 0x27U;
    d->steering_status_ms = d->drive_status_ms = now;
    d->steering_position_seen = d->left_rpm_seen = d->right_rpm_seen = d->encoder_seen = 1U;
    d->steering_position_ms = d->left_rpm_ms = d->right_rpm_ms = d->encoder_ms = now;
    d->encoder_config_valid = 1U;
    d->next_sdo_entry = 1U; /* steering position upload */
    for (unsigned i = 0U; i < PATROL_DIAG_SDO_ENTRY_COUNT; ++i) d->sdo_due_ms[i] = now;
}

// 丢失首个诊断响应只重读一次；59/60ms 边界、匹配检查、迟到无关帧及计时回绕均不产生写帧或伪造新样本。
static int test_diagnostic_read_retry(void)
{
    for (unsigned wrap = 0; wrap < 2U; ++wrap) {
        uint32_t start = wrap ? UINT32_MAX - 20U : 1000U;
        PatrolDiagnostics d;
        PatrolCanFrame first, retry, response;
        PatrolDiagnosticsSnapshot s;
        retry_fixture(&d, start);
        CHECK(PatrolDiagnostics_TakeSdoRead(&d, start, &first, 1U));
        CHECK(first.id == 0x601U && first.data[0] == 0x40U && first.data[1] == 0x64U);
        CHECK(!PatrolDiagnostics_TakeSdoRead(&d, start + 59U, &retry, 1U));
        CHECK(d.sdo_timeout_count == 0U && d.pending_retry == 0U);
        CHECK(PatrolDiagnostics_TakeSdoRead(&d, start + 60U, &retry, 1U));
        CHECK(!memcmp(&first, &retry, sizeof(first)) && d.pending_retry == 1U);
        CHECK(d.sdo_timeout_count == 1U && d.steering_position_ms == start);
        response = retry; response.id = 0x582U; response.data[0] = 0x43U;
        PatrolDiagnostics_OnCanFrame(&d, &response, start + 61U);
        CHECK(d.pending_valid && d.steering_position_ms == start);
        response.id = 0x581U; response.data[4] = 123U;
        PatrolDiagnostics_OnCanFrame(&d, &response, start + 62U);
        PatrolDiagnostics_Snapshot(&d, start + 62U, &s);
        CHECK(!d.pending_valid && d.pending_retry == 0U && s.sdo_timeout_count == 1U);
        CHECK(s.sdo_success_count == 1U && s.steering_feedback_fresh && s.drive_feedback_fresh);
        CHECK(d.steering_position_count == 123 && d.steering_position_ms == start + 62U);
    }
    return 0;
}

// 重试也无响应时释放总线给其他反馈；错误计数可上报，旧反馈仍会按真实时间过期，不因重试被延寿。
static int test_diagnostic_retry_exhausted(void)
{
    PatrolDiagnostics d;
    PatrolDiagnosticsSnapshot s;
    PatrolCanFrame first, frame;
    char json[900];
    retry_fixture(&d, 1000U);
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 1000U, &first, 1U));
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 1060U, &frame, 1U));
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 1119U, &frame, 1U));
    CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 1120U, &frame, 1U));
    CHECK(!d.pending_valid && d.sdo_timeout_count == 2U && d.steering_position_ms == 1000U);
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 1121U, &frame, 1U));
    CHECK(frame.data[0] == 0x40U && frame.id == 0x602U && frame.data[1] == 0x41U);
    PatrolDiagnostics_Snapshot(&d, 1251U, &s);
    CHECK(!s.steering_feedback_fresh && !s.drive_feedback_fresh);
    CHECK(PatrolDiagnostics_FormatCanJson(&s, 0U, 0U, 0U, 0U, 0U, json, sizeof(json)));
    CHECK(strstr(json, "\"sdoTimeouts\":2") != NULL);
    return 0;
}

// 反馈缺失、故障、时效余量不足或上层拒绝时不重试；不能为容错掩盖 OTA、Bus-Off、锁存等上层门禁。
static int test_diagnostic_retry_gate(void)
{
    for (unsigned reason = 0; reason < 12U; ++reason) {
        PatrolDiagnostics d;
        PatrolCanFrame frame;
        retry_fixture(&d, 1000U);
        CHECK(PatrolDiagnostics_TakeSdoRead(&d, 1000U, &frame, 1U));
        switch (reason) {
        case 0: d.steering_status_ms = 889U; break; /* 171ms old: reserve is insufficient */
        case 1: d.drive_status_ms = 889U; break;
        case 2: d.steering_position_ms = 889U; break;
        case 3: d.left_rpm_ms = 889U; break;
        case 4: d.right_rpm_ms = 889U; break;
        case 5: d.encoder_ms = 889U; break;
        case 6: d.steering_status_word |= 8U; break;
        case 7: d.drive_status_word |= 8U; break;
        case 8: d.drive_objects[0] = 1; break;
        case 9: d.drive_objects[1] = 1; break;
        case 10: d.encoder_seen = 0U; break;
        default: break; /* caller reports another safety gate */
        }
        CHECK(!PatrolDiagnostics_TakeSdoRead(&d, 1060U, &frame, reason != 11U));
        CHECK(!d.pending_valid && d.sdo_timeout_count == 1U && d.steering_position_ms != 1060U);
    }
    PatrolDiagnostics d;
    PatrolCanFrame frame;
    retry_fixture(&d, 1000U);
    d.steering_status_ms = 890U; /* exactly 170ms at retry: 60ms + 20ms remain */
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 1000U, &frame, 1U));
    CHECK(PatrolDiagnostics_TakeSdoRead(&d, 1060U, &frame, 1U));
    return 0;
}

int main(void)
{
    CHECK(test_sdo_waits_for_node() == 0);
    CHECK(test_status_snapshot_age() == 0);
    CHECK(test_sdo_safety_priority() == 0);
    CHECK(test_field_sdo_replies() == 0);
    CHECK(test_diagnostic_read_retry() == 0);
    CHECK(test_diagnostic_retry_exhausted() == 0);
    CHECK(test_diagnostic_retry_gate() == 0);
    ChassisControl control;
    ChassisFeedback feedback = {0};
    PatrolCanFrame frame;
    uint8_t request[8];
    uint8_t write_response[8];
    uint8_t response[9] = {1, 3, 4, 0x12, 0x34, 0x56, 0x78, 0, 0};
    uint16_t crc;
    uint32_t count;
    uint16_t config_values[3];
    uint8_t config_response[11] = {1, 3, 6, 0x00, 0x01, 0x00, 0x03, 0x4B, 0x00, 0, 0};
    PatrolDiagnostics diagnostics;
    PatrolDiagnosticsSnapshot diagnostic_snapshot;
    char fixed[24];
    ChassisCloudCommand command;
    const char json[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r1\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"acquire\",\"sessionId\":\"s1\",\"seq\":1,\"leaseMs\":1000,\"driveRpm\":1000,\"steeringDeg\":30}}}";
    const char over_limit[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r3\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"renew\",\"sessionId\":\"s1\",\"seq\":2,\"leaseMs\":1000,\"driveRpm\":200,\"steeringDeg\":30.001}}}";
    const char over_rpm[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r4\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"renew\",\"sessionId\":\"s1\",\"seq\":2,\"leaseMs\":1000,\"driveRpm\":1001,\"steeringDeg\":0}}}";
    const char fractional_rpm[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r5\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"renew\",\"sessionId\":\"s1\",\"seq\":2,\"leaseMs\":1000,\"driveRpm\":1.5,\"steeringDeg\":0}}}";
    const char fractional_lease[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r6\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"renew\",\"sessionId\":\"s1\",\"seq\":2,\"leaseMs\":999.5,\"driveRpm\":0,\"steeringDeg\":0}}}";
    const char sub_mdeg_steering[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r7\",\"deviceType\":\"vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"renew\",\"sessionId\":\"s1\",\"seq\":2,\"leaseMs\":1000,\"driveRpm\":0,\"steeringDeg\":1.0005}}}";
    const char invalid_params[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"r2\",\"command\":\"chassis_control\",\"params\":{}}}";
    const char power_on[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"p1\",\"deviceType\":\"vehicle_chassis\",\"command\":\"power_on\",\"expiresAt\":1788838338000,\"params\":null}}";
    const char power_bad_params[] = "{\"version\":\"4.0\",\"timestamp\":1788838333000,\"data\":{\"requestId\":\"p2\",\"deviceType\":\"vehicle_chassis\",\"command\":\"power_off\",\"params\":{}}}";

    CHECK(SteeringAngle_FromControllerCount(-58015, -85123, 2710800U, 1) == 10000);
    CHECK(SteeringAngle_FromControllerCount(-112231, -85123, 2710800U, 1) == -10000);
    CHECK(SteeringAngle_FromAbsoluteCount(3734U, 3182U, 55250U, 1) == 9990);
    CHECK(SteeringAngle_FromAbsoluteCount(2629U, 3182U, 55250U, 1) == -10009);
    CHECK(SteeringAngle_FromAbsoluteCount(2629U, 3182U, 55250U, -1) == 10009);
    /* Position-source loss invalidates the coordinate.  Unrelated drive/status
     * gates only block the next motion request and do not fault an idle axis. */
    CHECK(SteeringCoordinate_EpochLost(1U, 1U, 1U));
    CHECK(SteeringCoordinate_EpochLost(4U, 1U, 1U));
    CHECK(SteeringCoordinate_EpochLost(32U, 1U, 1U));
    CHECK(SteeringCoordinate_EpochLost(0U, 0U, 1U));
    CHECK(SteeringCoordinate_EpochLost(0U, 1U, 0U));
    CHECK(!SteeringCoordinate_EpochLost(2U | 8U | 16U | 64U | 128U, 1U, 1U));
    {
        int32_t angle = 0;
        CHECK(SteeringAngle_AbsoluteWithinLimit(0U, 0U, 55250U, 1, 30000, &angle) && angle == 0);
        CHECK(SteeringAngle_AbsoluteWithinLimit(4095U, 0U, 55250U, 1, 30000, &angle) && angle == -18);
        CHECK(SteeringAngle_AbsoluteWithinLimit(1U, 0U, 55250U, 1, 30000, &angle) && angle == 18);
        CHECK(SteeringAngle_AbsoluteWithinLimit(2439U, 0U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(2438U, 0U, 55250U, 1, 30000, &angle));
        CHECK(SteeringAngle_AbsoluteWithinLimit(1657U, 0U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(1658U, 0U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(4096U, 0U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(2048U, 0U, 55250U, 1, 30000, &angle));
        CHECK(SteeringAngle_AbsoluteWithinLimit(2048U, 2048U, 55250U, 1, 30000, &angle) && angle == 0);
        CHECK(SteeringAngle_AbsoluteWithinLimit(391U, 2048U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(390U, 2048U, 55250U, 1, 30000, &angle));
        CHECK(SteeringAngle_AbsoluteWithinLimit(3705U, 2048U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(3706U, 2048U, 55250U, 1, 30000, &angle));
        CHECK(!SteeringAngle_AbsoluteWithinLimit(4096U, 2048U, 55250U, 1, 30000, &angle));
    }
    {
        int32_t accumulated = 0;
        CHECK(SteeringAngle_AccumulateSingleTurn(4090U, 5U, 4096U, 256U, &accumulated));
        CHECK(accumulated == 11);
        CHECK(SteeringAngle_AccumulateSingleTurn(5U, 4090U, 4096U, 256U, &accumulated));
        CHECK(accumulated == 0);
        CHECK(!SteeringAngle_AccumulateSingleTurn(100U, 400U, 4096U, 256U, &accumulated));
        CHECK(accumulated == 0);
        CHECK(!SteeringAngle_AccumulateSingleTurn(4096U, 0U, 4096U, 256U, &accumulated));
    }

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
    AbsoluteEncoder_BuildWriteSingleRequest(3U, 15U, write_response);
    CHECK(write_response[0] == 1U && write_response[1] == 6U && write_response[3] == 3U &&
          write_response[4] == 0U && write_response[5] == 15U);
    CHECK(AbsoluteEncoder_ParseWriteSingleResponse(write_response, sizeof(write_response), 3U, 15U));
    write_response[5] = 1U;
    CHECK(!AbsoluteEncoder_ParseWriteSingleResponse(write_response, sizeof(write_response), 3U, 15U));
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
        int32_t value = sample == 1U ? 1000 : (sample == 3U ? -12 : (sample == 4U ? 15 : 0x21));
        CHECK(PatrolDiagnostics_TakeSdoRead(&diagnostics, 110U + sample, &frame, 1U));
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
    PatrolDiagnostics_OnEncoderError(&diagnostics, PATROL_ENCODER_ERROR_CRC);
    PatrolDiagnostics_OnEncoderError(&diagnostics, PATROL_ENCODER_ERROR_TRANSPORT);
    PatrolDiagnostics_Snapshot(&diagnostics, 130U, &diagnostic_snapshot);
    CHECK(diagnostic_snapshot.steering_online && diagnostic_snapshot.drive_online);
    CHECK(diagnostic_snapshot.steering_feedback_fresh && diagnostic_snapshot.drive_feedback_fresh);
    CHECK(diagnostic_snapshot.encoder_online && diagnostic_snapshot.encoder_config_valid);
    CHECK(diagnostic_snapshot.steering_position_count == 1000);
    CHECK(diagnostic_snapshot.left_actual_rpm == -12 && diagnostic_snapshot.right_actual_rpm == 15);
    CHECK(diagnostic_snapshot.encoder_count == 123456U);
    CHECK(diagnostic_snapshot.encoder_crc_error_count == 1U);
    CHECK(diagnostic_snapshot.encoder_transport_error_count == 1U);
    CHECK(diagnostic_snapshot.last_encoder_error == PATROL_ENCODER_ERROR_TRANSPORT);
    CHECK(PatrolDiagnostics_TakeSdoRead(&diagnostics, 200U, &frame, 1U));
    CHECK(!PatrolDiagnostics_TakeSdoRead(&diagnostics, 250U, &frame, 1U));
    CHECK(PatrolDiagnostics_TakeSdoRead(&diagnostics, 260U, &frame, 1U));
    CHECK(frame.data[0] == 0x40U);

    ChassisControl_Init(&control);
    feedback.calibrated = 1; feedback.encoder_range_valid = 1; feedback.encoder_profile_ready = 1;
    feedback.steering_online = 1; feedback.drive_online = 1;
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

    ChassisControl_Init(&control);
    feedback.feedback_ms = 0; feedback.left_actual_rpm = 50; feedback.right_actual_rpm = 50;
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "acquire", "reverse", 1, 1000, 50, 0, 0));
    CHECK(ChassisControl_Apply(&control, "renew", "reverse", 2, 1000, -50, 0, 10));
    feedback.feedback_ms = 900; ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "renew", "reverse", 3, 1000, -50, 0, 900));
    feedback.feedback_ms = 1800; ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "renew", "reverse", 4, 1000, -50, 0, 1800));
    /* Renew the lease while preserving the original reversal start at 10 ms. */
    for (uint32_t t=2500; t<=6000; t+=500) {
        feedback.feedback_ms=t; ChassisControl_SetFeedback(&control,&feedback);
        CHECK(ChassisControl_Apply(&control,"renew","reverse",5+t,1000,-50,0,t));
        ChassisControl_Tick(&control,t);
        CHECK(control.reversal_pending && !control.fault_latched && control.target_rpm==0);
    }
    feedback.feedback_ms = 6010; ChassisControl_SetFeedback(&control, &feedback);
    ChassisControl_Tick(&control, 6010);
    CHECK(control.fault_latched && control.stop_reason == CHASSIS_STOP_DIRECTION_TIMEOUT);
    CHECK(!ChassisControl_Apply(&control, "acquire", "again", 1, 1000, 50, 0, 6010));

    ChassisControl_Init(&control);
    memset(&feedback, 0, sizeof(feedback));
    feedback.calibrated = 1; feedback.encoder_range_valid = 1; feedback.encoder_profile_ready = 1;
    feedback.steering_online = 1; feedback.drive_online = 1; feedback.power_ready = 1;
    feedback.feedback_ms = 3000;
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "acquire", "range", 1, 1000, 20, 1000, 3000));
    feedback.encoder_range_valid = 0; feedback.feedback_ms = 3010;
    ChassisControl_SetFeedback(&control, &feedback);
    ChassisControl_Tick(&control, 3010);
    CHECK(control.fault_latched && control.stop_reason == CHASSIS_STOP_ENCODER_RANGE);
    CHECK(control.target_rpm == 0 && control.target_steering_mdeg == 0 && !control.enable_request);

    ChassisControl_Init(&control);
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(!ChassisControl_Apply(&control, "acquire", "blocked", 1, 1000, 50, 0, 0));
    CHECK(control.target_rpm == 0);


    g_power_level = 1U;
    VehiclePowerOutput_Init(test_power_write);
    CHECK(VehiclePowerOutput_IsAvailable() && !VehiclePowerOutput_IsOn() && g_power_level == 0U);
    CHECK(VehiclePowerOutput_Set(1U) && VehiclePowerOutput_IsOn() && g_power_level == 1U);
    CHECK(VehiclePowerOutput_Set(0U) && !VehiclePowerOutput_IsOn() && g_power_level == 0U);

    CHECK(ChassisCloud_Parse(json, (uint16_t)strlen(json), &command) == CHASSIS_CLOUD_OK);
    CHECK(command.type == CHASSIS_CLOUD_COMMAND_MOTION && command.drive_rpm == 1000 &&
          command.steering_mdeg == 30000 && strcmp(command.session_id, "s1") == 0);
    CHECK(ChassisCloud_Parse(power_on, (uint16_t)strlen(power_on), &command) == CHASSIS_CLOUD_OK);
    CHECK(command.type == CHASSIS_CLOUD_COMMAND_POWER_ON && strcmp(command.request_id, "p1") == 0);
    char ttl[256];
    for (unsigned duration=0; duration<=5001; duration+= duration == 0 ? 5000 : 1) {
        snprintf(ttl,sizeof(ttl),"{\"version\":\"4.0\",\"timestamp\":10000,\"data\":{\"requestId\":\"ttl\",\"command\":\"power_on\",\"expiresAt\":%u,\"params\":null}}",10000+duration);
        CHECK(ChassisCloud_Parse(ttl,(uint16_t)strlen(ttl),&command) ==
            (duration == 5000 ? CHASSIS_CLOUD_OK : CHASSIS_CLOUD_INVALID));
    }
    CHECK(ChassisCloud_Parse(power_bad_params, (uint16_t)strlen(power_bad_params), &command) ==
          CHASSIS_CLOUD_INVALID);
    CHECK(ChassisCloud_Parse(over_limit, (uint16_t)strlen(over_limit), &command) == CHASSIS_CLOUD_INVALID);
    CHECK(ChassisCloud_Parse(over_rpm, (uint16_t)strlen(over_rpm), &command) == CHASSIS_CLOUD_INVALID);
    CHECK(ChassisCloud_Parse(fractional_rpm, (uint16_t)strlen(fractional_rpm), &command) == CHASSIS_CLOUD_INVALID);
    CHECK(ChassisCloud_Parse(fractional_lease, (uint16_t)strlen(fractional_lease), &command) == CHASSIS_CLOUD_INVALID);
    CHECK(ChassisCloud_Parse(sub_mdeg_steering, (uint16_t)strlen(sub_mdeg_steering), &command) == CHASSIS_CLOUD_INVALID);
    CHECK(ChassisCloud_Parse(invalid_params, (uint16_t)strlen(invalid_params), &command) ==
          CHASSIS_CLOUD_INVALID);
    /* Both signs: small overshoot remains controllable inward, not a fault. */
    for (int sign=-1; sign<=1; sign+=2) {
        ChassisControl_Init(&control); memset(&feedback,0,sizeof(feedback));
        feedback.calibrated=feedback.encoder_profile_ready=feedback.encoder_range_valid=1;
        feedback.steering_online=feedback.drive_online=feedback.power_ready=1;
        feedback.feedback_ms=100; feedback.steering_actual_mdeg=sign*29963;
        feedback.encoder_actual_mdeg=sign*30045;
        ChassisControl_SetFeedback(&control,&feedback);
        CHECK(ChassisControl_Apply(&control,"acquire","edge",1,1000,50,sign*30000,100));
        CHECK(!control.fault_latched && control.steering_boundary==sign && control.target_rpm==0);
        CHECK(control.target_steering_mdeg==sign*29500);
        feedback.steering_actual_mdeg=feedback.encoder_actual_mdeg=sign*29400;
        ChassisControl_SetFeedback(&control,&feedback);
        CHECK(ChassisControl_Apply(&control,"renew","edge",2,1000,50,sign*30000,110));
        CHECK(control.steering_boundary==sign && control.target_steering_mdeg==sign*29500);
        ChassisControl_Tick(&control,120); CHECK(control.steering_boundary==sign);
        CHECK(ChassisControl_Apply(&control,"renew","edge",3,1000,0,0,130));
        CHECK(!control.steering_boundary && control.target_steering_mdeg==0);
        feedback.steering_actual_mdeg=sign*30501;feedback.encoder_actual_mdeg=sign*30500;
        ChassisControl_SetFeedback(&control,&feedback); ChassisControl_Tick(&control,140);
        CHECK(control.fault_latched && !control.enable_request && control.stop_reason==CHASSIS_STOP_ENCODER_RANGE);
        CHECK(!ChassisControl_Apply(&control,"renew","edge",4,1000,0,0,150));
        ChassisControl_Init(&control);feedback.steering_actual_mdeg=sign*30045;feedback.encoder_actual_mdeg=sign*30045;
        feedback.feedback_ms=0;ChassisControl_SetFeedback(&control,&feedback);
        CHECK(!ChassisControl_Apply(&control,"acquire","stale",1,1000,0,0,500));
        feedback.feedback_ms=500;feedback.drive_fault=1;ChassisControl_SetFeedback(&control,&feedback);
        CHECK(!ChassisControl_Apply(&control,"acquire","fault",1,1000,0,0,500));
    }
    return 0;
}
