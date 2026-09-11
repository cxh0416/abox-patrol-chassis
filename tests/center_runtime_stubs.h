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
#include "vcu_config.h"
#include "vehicle_power_output.h"
#include "vehicle_steering_profile.h"

#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define APP_LOGE(...) ((void)0)
#define APP_LOGW(...) ((void)0)
#define CENTER_CAL_READY_LEASE_MS 15000U
#define PATROL_CHASSIS_BENCH_STEERING_ENABLED 1

static uint32_t mock_now;
static uint8_t mock_power, mock_bus_off, mock_ota, mock_permit;
static ChassisControl mock_control;
static VcuRemoteConfig_t mock_config;
static PatrolDiagnosticsSnapshot mock_snapshot;
static PatrolDiagnostics g_diagnostics;
static BenchSteering g_bench;
static BenchDrive g_drive;
static SteeringCoordinate g_coordinate;
static uint16_t g_feedback_gate_mask, g_bench_fault_gate_mask;
static uint8_t g_coordinate_reset_requested;
static int32_t g_coordinate_calibration_center;
static uint32_t g_coordinate_absolute_center;

uint32_t HAL_GetTick(void) { return mock_now; }
const ChassisControl* RemoteService_State(void) { return &mock_control; }
uint8_t RemoteService_BenchPermitted(void) { return mock_permit; }
void RemoteService_BenchRevoke(void) { mock_permit = 0; }
void RemoteService_BenchFault(void) { mock_control.fault_latched = 1; }
uint8_t VehiclePowerOutput_IsOn(void) { return mock_power; }
uint8_t VehiclePowerOutput_Set(uint8_t on) { mock_power = on; return 1; }
static uint8_t VcuRtos_CanBusOff(void) { return mock_bus_off; }
static uint8_t VcuRtos_CanBusOffActive(void) { return mock_bus_off; }
static uint8_t OtaService_IsBusy(void) { return mock_ota; }
static void VcuRtos_GetDiagnostics(PatrolDiagnosticsSnapshot* s) { *s = mock_snapshot; }
void PatrolDiagnostics_Snapshot(const PatrolDiagnostics* d, uint32_t now, PatrolDiagnosticsSnapshot* s)
{ (void)d; (void)now; *s = mock_snapshot; }
const VcuRemoteConfig_t* VcuConfig_Get(void) { return &mock_config; }
uint8_t VcuConfig_SingleTurnProfileReady(void) { return 0; }
uint8_t VcuConfig_ClearCalibration(void) { return 1; }
uint8_t VcuConfig_SetCalibration(int32_t driver, uint32_t encoder, uint32_t dc, uint32_t ec,
    int8_t dd, int8_t ed, uint16_t tolerance)
{ (void)driver; (void)encoder; (void)dc; (void)ec; (void)dd; (void)ed; (void)tolerance; return 1; }
