#include "motion_internal.h"
#include "app_log.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_steering.h"
#include "bench_drive.h"
#include "steering_angle.h"
#include "patrol_diagnostics.h"
#include "remote_service.h"
#include "patrol_config.h"
#include "vehicle_power_output.h"
#include "vehicle_steering_profile.h"

uint32_t mock_now;
uint8_t mock_power, mock_bus_off, mock_ota, mock_permit;
uint8_t mock_can_ready, mock_revoke_before_send, mock_sent;
ChassisControl mock_control;
PatrolRemoteConfigV5_t mock_config;
PatrolDiagnosticsSnapshot mock_snapshot;
uint32_t PatrolRuntimePort_Now(void) { return mock_now; }
const ChassisControl* RemoteService_State(void) { return &mock_control; }
uint8_t RemoteService_BenchPermitted(void) { return mock_permit; }
void RemoteService_BenchRevoke(void) { mock_permit = 0; }
void RemoteService_BenchFault(void) { mock_control.fault_latched = 1; }
uint8_t VehiclePowerOutput_IsOn(void) { return mock_power; }
uint8_t VehiclePowerOutput_Set(uint8_t on) { mock_power = on; return 1; }
uint8_t PatrolRuntimePort_CanBusOff(void) { return mock_bus_off; }
uint8_t PatrolRuntimePort_CanBusOffActive(void) { return mock_bus_off; }
void PatrolRtos_ObserveCanRecovery(uint32_t sample_ms, uint8_t healthy)
{ (void)sample_ms; (void)healthy; }
uint8_t PatrolRuntimePort_OtaBusy(void) { return mock_ota; }
#ifndef CENTER_HOST_REAL_DIAGNOSTICS
void PatrolDiagnostics_Snapshot(const PatrolDiagnostics* d, uint32_t now, PatrolDiagnosticsSnapshot* s)
{ (void)d; (void)now; *s = mock_snapshot; }
void PatrolDiagnostics_Init(PatrolDiagnostics* d) { memset(d, 0, sizeof(*d)); }
void PatrolDiagnostics_OnCanFrame(PatrolDiagnostics* d, const PatrolCanFrame* f, uint32_t now)
{ (void)d; (void)f; (void)now; }
uint8_t PatrolDiagnostics_TakeSdoRead(PatrolDiagnostics* d, uint32_t now, PatrolCanFrame* f)
{ (void)d; (void)now; (void)f; return 0; }
void PatrolDiagnostics_OnSdoTxError(PatrolDiagnostics* d, uint32_t now) { (void)d; (void)now; }
void PatrolDiagnostics_OnEncoderConfig(PatrolDiagnostics* d, const uint16_t* r, uint32_t now)
{ (void)d; (void)r; (void)now; }
void PatrolDiagnostics_OnEncoderCount(PatrolDiagnostics* d, uint32_t c, uint32_t now)
{ (void)d; (void)c; (void)now; }
void PatrolDiagnostics_OnEncoderError(PatrolDiagnostics* d, PatrolEncoderError e) { (void)d; (void)e; }
#endif
const PatrolRemoteConfigV5_t* PatrolConfig_Get(void) { return &mock_config; }
uint8_t PatrolConfig_SingleTurnProfileReady(void) { return 0; }
uint8_t PatrolConfig_ClearCalibration(void) { return 1; }
uint8_t PatrolConfig_SetCalibration(int32_t driver, uint32_t encoder, uint32_t dc, uint32_t ec,
    int8_t dd, int8_t ed, uint16_t tolerance)
{ (void)driver; (void)encoder; (void)dc; (void)ec; (void)dd; (void)ed; (void)tolerance; return 1; }

void PatrolRuntimePort_Enter(void) {}
void PatrolRuntimePort_Exit(void) {}
void AppLog_Printf(AppLogLevel_t level, const char* tag, const char* format, ...)
{ (void)level; (void)tag; (void)format; }
uint8_t PatrolConfig_CalibrationCompatible(void) { return 1; }
void RemoteService_UpdateFeedback(const ChassisFeedback* feedback) { mock_control.feedback = *feedback; }
uint8_t RemoteService_BenchArm(uint16_t limit, const char* request_id, uint32_t now) { (void)limit; (void)request_id; (void)now; return 0; }
uint8_t RemoteService_BenchDriveArm(uint16_t limit, const char* request_id, uint32_t now) { (void)limit; (void)request_id; (void)now; return 0; }
uint8_t RemoteService_BenchDrivePermitted(void) { return 0; }
const char* RemoteService_BenchSession(void) { return ""; }
uint16_t RemoteService_BenchLimit(void) { return 30000; }
void PatrolRuntimePort_AbortCanTx(void) {}
uint8_t PatrolRuntimePort_CanTxReady(void)
{
    /* Model the higher priority safety task revoking a selected command. */
    if (mock_revoke_before_send) mock_permit = 0;
    return mock_can_ready;
}
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* frame)
{ (void)frame; mock_sent++; return 1; }

uint8_t OTA_Info_LoadMotionFault(MotionFaultRecord* r) { (void)r; return 0; }
uint8_t OTA_Info_SaveMotionFault(const MotionFaultRecord* r) { (void)r; return 1; }

void RemoteService_Init(void) { memset(&mock_control,0,sizeof(mock_control)); mock_permit=0; }
