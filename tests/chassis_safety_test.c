#include <stdio.h>
#include <string.h>

#include "absolute_encoder.h"
#include "chassis_cloud_protocol.h"
#include "chassis_control.h"
#include "chassis_power.h"
#include "patrol_canopen.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    ChassisControl control;
    ChassisFeedback feedback = {0};
    PatrolCanFrame frame;
    ChassisPower power;
    uint8_t request[8];
    uint8_t response[9] = {1, 3, 4, 0x12, 0x34, 0x56, 0x78, 0, 0};
    uint16_t crc;
    uint32_t count;
    ChassisCloudCommand command;
    const char json[] = "{\"version\":\"3.0\",\"data\":{\"requestId\":\"r1\",\"deviceType\":\"patrol_vehicle_chassis\",\"command\":\"chassis_control\",\"params\":{\"action\":\"acquire\",\"sessionId\":\"s1\",\"seq\":1,\"leaseMs\":1000,\"driveRpm\":50,\"steeringDeg\":1}}}";

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

    ChassisControl_Init(&control, 1);
    feedback.calibrated = 1; feedback.steering_online = 1; feedback.drive_online = 1;
    feedback.power_ready = 1; feedback.feedback_ms = 100;
    ChassisControl_SetFeedback(&control, &feedback);
    CHECK(ChassisControl_Apply(&control, "acquire", "session", 1, 1000, 150, 3000, 100));
    CHECK(control.target_rpm == 150 && control.target_steering_mdeg == 3000);
    CHECK(ChassisControl_Apply(&control, "renew", "session", 2, 1000, -50, -1000, 200));
    CHECK(control.target_rpm == 0 && control.reversal_pending);
    feedback.feedback_ms = 220; ChassisControl_SetFeedback(&control, &feedback); ChassisControl_Tick(&control, 220);
    feedback.feedback_ms = 230; ChassisControl_SetFeedback(&control, &feedback); ChassisControl_Tick(&control, 230);
    feedback.feedback_ms = 240; ChassisControl_SetFeedback(&control, &feedback); ChassisControl_Tick(&control, 240);
    CHECK(control.target_rpm == -50 && !control.reversal_pending);
    feedback.feedback_ms = 1300; ChassisControl_SetFeedback(&control, &feedback);
    ChassisControl_Tick(&control, 1300);
    CHECK(control.target_rpm == 0 && control.stop_reason == CHASSIS_STOP_LEASE_EXPIRED);

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

    CHECK(ChassisCloud_Parse(json, (uint16_t)strlen(json), &command));
    CHECK(command.drive_rpm == 50 && command.steering_mdeg == 1000 && strcmp(command.session_id, "s1") == 0);
    return 0;
}
