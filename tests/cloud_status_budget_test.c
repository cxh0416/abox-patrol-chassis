/* Run the production encoder with long metadata and storage boundary values. */
#include "../app/Core/User/cloud/src/cloud_messages.c"
#include <assert.h>
#include <limits.h>

CloudContext g_cloud;
static ChassisControl control;
static PatrolRemoteConfigV5_t config;
uint32_t CloudPort_Now(void) { return UINT32_MAX; }
const ChassisControl* RemoteService_State(void) { return &control; }
const PatrolRemoteConfigV5_t* PatrolConfig_Get(void) { return &config; }
uint8_t TimeBase_FormatUtcMs(char* output, uint16_t size)
{ return snprintf(output, size, "18446744073709551615") < size; }
void PatrolRuntime_GetDiagnostics(PatrolDiagnosticsSnapshot* snapshot)
{ memset(snapshot, 0xFF, sizeof(*snapshot)); }
uint8_t ChassisControl_SafetyReady(const ChassisControl* value, uint32_t now)
{ (void)value; (void)now; return 0U; }
uint8_t VehiclePowerOutput_IsOn(void) { return 0U; }

int main(void)
{
    char output[CLOUD_PAYLOAD_SIZE];
    char request_id[65];
    memset(config.vid, 'V', sizeof(config.vid) - 1U);
    memset(request_id, 'R', sizeof(request_id) - 1U);
    request_id[sizeof(request_id) - 1U] = '\0';
    control.target_rpm = -CHASSIS_MAX_RPM;
    control.target_steering_mdeg = -CHASSIS_MAX_STEERING_MDEG;
    control.feedback.left_actual_rpm = INT16_MIN;
    control.feedback.right_actual_rpm = INT16_MIN;
    control.feedback.steering_actual_mdeg = INT32_MIN;
    control.feedback.encoder_actual_mdeg = INT32_MIN;
    control.steering_boundary = -1;
    control.stop_reason = CHASSIS_STOP_ENCODER_PROFILE;
    for (unsigned sync = 0U; sync < 2U; ++sync) {
        g_cloud.status_report_request_id = sync ? request_id : NULL;
        for (unsigned fault = 0U; fault < 2U; ++fault) {
            g_cloud.status_report_sequence = UINT32_MAX;
            control.fault_latched = fault;
            assert(CloudMessages_BuildStatusReport(output, sizeof(output), "fault_cleared"));
            assert(strlen(output) <= 1024U);
            assert(strstr(output, "\"powerOutputOn\":false"));
            assert(!strstr(output, "transportDiagnostics") && !strstr(output, "lastFault"));
            puts(output);
        }
    }
    assert(!CloudMessages_BuildStatusReport(output, 128U, "periodic"));
    return 0;
}
