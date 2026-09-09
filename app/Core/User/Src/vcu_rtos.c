#include "vcu_rtos.h"

#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "boot_state.h"
#include "app_log.h"
#include "app_test.h"
#include "absolute_encoder.h"
#include "chassis_power.h"
#include "cloud_service.h"
#include "fault_mailbox.h"
#include "iwdg.h"
#include "ota_service.h"
#include "patrol_canopen.h"
#include "patrol_diagnostics.h"
#include "remote_service.h"
#include "steering_angle.h"
#include "bench_steering.h"
#include "bench_drive.h"
#ifndef PATROL_CHASSIS_BENCH_DRIVE_ENABLED
#define PATROL_CHASSIS_BENCH_DRIVE_ENABLED 0
#endif
#include <stdlib.h>
#include "task.h"
#include "usart.h"
#include "vcu_config.h"
#include "vehicle_power_output.h"
#include "vehicle_steering_profile.h"

#ifndef PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
#define PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED 0
#endif

#define VCU_HEALTH_SAFETY  (1U << 0)
#define VCU_HEALTH_CAN_RX  (1U << 1)
#define VCU_HEALTH_CANOPEN  (1U << 2)
#define VCU_EVENT_CAN_ERROR (1U << 3)
#define VCU_HEALTH_REQUIRED (VCU_HEALTH_SAFETY | VCU_HEALTH_CAN_RX | VCU_HEALTH_CANOPEN)

#define VCU_SAFETY_STACK_WORDS 256U
#define VCU_CAN_RX_STACK_WORDS 256U
#define VCU_CANOPEN_STACK_WORDS 256U
#define VCU_ENCODER_STACK_WORDS 192U
#define VCU_POWER_STACK_WORDS 192U
#define VCU_REMOTE_STACK_WORDS 1024U
#define VCU_DIAG_STACK_WORDS 256U
/*
 * BootState_Confirm() persists a Boot V2 record from this task.  The ARM
 * release stack reports show a 992-byte static call-chain peak before the
 * HAL flash helper margin, so 256 words is not sufficient for trial confirm.
 */
#define VCU_WATCHDOG_STACK_WORDS 384U

static StaticQueue_t g_can_rx_queue_control;
static VcuCanFrame_t g_can_rx_queue_storage[VCU_CAN_RX_QUEUE_LENGTH];
static QueueHandle_t g_can_rx_queue;
static StaticEventGroup_t g_health_control;
static EventGroupHandle_t g_health_events;

static StaticTask_t g_safety_tcb;
static StaticTask_t g_can_rx_tcb;
static StaticTask_t g_canopen_tcb;
static StaticTask_t g_encoder_tcb;
static StaticTask_t g_power_tcb;
static StaticTask_t g_remote_tcb;
static StaticTask_t g_diag_tcb;
static StaticTask_t g_watchdog_tcb;
static StackType_t g_safety_stack[VCU_SAFETY_STACK_WORDS];
static StackType_t g_can_rx_stack[VCU_CAN_RX_STACK_WORDS];
static StackType_t g_canopen_stack[VCU_CANOPEN_STACK_WORDS];
static StackType_t g_encoder_stack[VCU_ENCODER_STACK_WORDS];
static StackType_t g_power_stack[VCU_POWER_STACK_WORDS];
static StackType_t g_remote_stack[VCU_REMOTE_STACK_WORDS];
static StackType_t g_diag_stack[VCU_DIAG_STACK_WORDS];
static StackType_t g_watchdog_stack[VCU_WATCHDOG_STACK_WORDS];
static TaskHandle_t g_safety_task;
static TaskHandle_t g_can_rx_task;
static TaskHandle_t g_canopen_task;
static TaskHandle_t g_encoder_task;
static TaskHandle_t g_power_task;
static TaskHandle_t g_remote_task;
static TaskHandle_t g_diag_task;
static TaskHandle_t g_watchdog_task;
static volatile uint32_t g_can_rx_overflow_count;
static volatile uint32_t g_can_raw_rx_count[2];
static volatile uint8_t g_can_started;
static volatile uint8_t g_can_bus_off_mask;
static uint8_t g_boot_confirmed;
static PatrolDiagnostics g_diagnostics;
static BenchSteering g_bench;
static BenchDrive g_drive;
static uint8_t g_drive_turn;
static uint8_t g_bench_turn;
static uint8_t g_bench_transport_stopped;
static uint16_t g_feedback_gate_mask, g_bench_fault_gate_mask;
static SteeringCoordinate g_coordinate;
static uint8_t g_coordinate_reset_requested;
static int32_t g_coordinate_calibration_center;
static uint32_t g_coordinate_absolute_center;

typedef enum {
    CENTER_CAL_IDLE = 0,
    CENTER_CAL_READY,
    CENTER_CAL_JOGGING,
    CENTER_CAL_STOPPING,
    CENTER_CAL_COMMIT_STABLE,
    CENTER_CAL_ENCODER_WRITE,
    CENTER_CAL_ENCODER_VERIFY,
    CENTER_CAL_COMPLETE,
    CENTER_CAL_FAILED,
    CENTER_CAL_START_STABLE
} CenterCalibrationPhase;

typedef struct {
    CenterCalibrationPhase phase;
    char session_id[40];
    uint32_t serial;
    uint32_t deadline_ms;
    uint32_t last_sample_ms;
    uint32_t fault;
    int32_t start_mdeg;
    int32_t target_mdeg;
    int32_t accumulated_mdeg;
    int32_t captured_driver_count;
    uint8_t stable_samples;
    volatile uint8_t encoder_write_state; /* 0 idle, 1 requested, 2 echoed, 3 not sent, 4 executing, 5 uncertain. */
} CenterCalibration;

static CenterCalibration g_center_cal;
static uint32_t g_center_cal_serial;
#ifndef PATROL_CHASSIS_BENCH_STEERING_ENABLED
#define PATROL_CHASSIS_BENCH_STEERING_ENABLED 0
#endif

static void Vcu_SafetyControlTask(void *argument);
static void Vcu_CanRxTask(void *argument);
static void Vcu_CanopenTask(void *argument);
static void Vcu_EncoderTask(void *argument);
static void Vcu_PowerTask(void *argument);
static void Vcu_RemoteTask(void *argument);
static void Vcu_DiagTask(void *argument);
static void Vcu_WatchdogTask(void *argument);
static void center_calibration_tick(uint32_t now);

static uint8_t vcu_can_bus_index(const CAN_HandleTypeDef *hcan)
{
    return (hcan != NULL && hcan->Instance == CAN2) ? 1U : 0U;
}

static void vcu_start_can(void)
{
    const uint32_t notifications = CAN_IT_RX_FIFO0_MSG_PENDING |
                                   CAN_IT_ERROR | CAN_IT_BUSOFF |
                                   CAN_IT_LAST_ERROR_CODE;

    if (g_can_started != 0U) return;
    if (HAL_CAN_Start(&hcan1) != HAL_OK) Error_Handler();
    if (HAL_CAN_Start(&hcan2) != HAL_OK) Error_Handler();
    if (HAL_CAN_ActivateNotification(&hcan1, notifications) != HAL_OK) Error_Handler();
    if (HAL_CAN_ActivateNotification(&hcan2, notifications) != HAL_OK) Error_Handler();
    g_can_started = 1U;
}

void VcuRtos_Init(void)
{
    PatrolDiagnostics_Init(&g_diagnostics);
    g_can_rx_queue = xQueueCreateStatic(VCU_CAN_RX_QUEUE_LENGTH,
                                         sizeof(VcuCanFrame_t),
                                         (uint8_t *)g_can_rx_queue_storage,
                                         &g_can_rx_queue_control);
    g_health_events = xEventGroupCreateStatic(&g_health_control);

    g_safety_task = xTaskCreateStatic(Vcu_SafetyControlTask, "SafetyControl",
                                      VCU_SAFETY_STACK_WORDS, NULL, 6U,
                                      g_safety_stack, &g_safety_tcb);
    g_can_rx_task = xTaskCreateStatic(Vcu_CanRxTask, "CanRx",
                                      VCU_CAN_RX_STACK_WORDS, NULL, 5U,
                                      g_can_rx_stack, &g_can_rx_tcb);
    g_canopen_task = xTaskCreateStatic(Vcu_CanopenTask, "Canopen",
                                      VCU_CANOPEN_STACK_WORDS, NULL, 4U,
                                      g_canopen_stack, &g_canopen_tcb);
    g_encoder_task = xTaskCreateStatic(Vcu_EncoderTask, "EncoderUart5",
                                       VCU_ENCODER_STACK_WORDS, NULL, 4U,
                                       g_encoder_stack, &g_encoder_tcb);
    g_power_task = xTaskCreateStatic(Vcu_PowerTask, "ChassisPower",
                                     VCU_POWER_STACK_WORDS, NULL, 4U,
                                     g_power_stack, &g_power_tcb);
    g_watchdog_task = xTaskCreateStatic(Vcu_WatchdogTask, "Watchdog",
                                        VCU_WATCHDOG_STACK_WORDS, NULL, 3U,
                                        g_watchdog_stack, &g_watchdog_tcb);
    g_remote_task = xTaskCreateStatic(Vcu_RemoteTask, "Remote",
                                      VCU_REMOTE_STACK_WORDS, NULL, 2U,
                                      g_remote_stack, &g_remote_tcb);
    g_diag_task = xTaskCreateStatic(Vcu_DiagTask, "Diag",
                                    VCU_DIAG_STACK_WORDS, NULL, 1U,
                                    g_diag_stack, &g_diag_tcb);

    configASSERT(g_can_rx_queue != NULL);
    configASSERT(g_health_events != NULL);
    configASSERT(g_safety_task != NULL && g_can_rx_task != NULL && g_canopen_task != NULL);
    configASSERT(g_encoder_task != NULL && g_power_task != NULL);
    configASSERT(g_watchdog_task != NULL && g_remote_task != NULL && g_diag_task != NULL);

#if defined(DEBUG)
    __HAL_DBGMCU_FREEZE_IWDG();
#endif
}

BaseType_t VcuRtos_CanRxFromISR(CAN_HandleTypeDef *hcan, BaseType_t *higher_priority_task_woken)
{
    CAN_RxHeaderTypeDef header;
    VcuCanFrame_t frame;

    if (hcan == NULL || g_can_rx_queue == NULL) return pdFAIL;
    memset(&frame, 0, sizeof(frame));
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, frame.data) != HAL_OK) return pdFAIL;
    frame.bus = vcu_can_bus_index(hcan);
    g_can_raw_rx_count[frame.bus]++;
    frame.header = header;
    frame.timestamp_ms = HAL_GetTick();

    if (xQueueSendFromISR(g_can_rx_queue, &frame, higher_priority_task_woken) != pdPASS)
    {
        g_can_rx_overflow_count++;
    }
    if (g_can_rx_task != NULL)
    {
        vTaskNotifyGiveFromISR(g_can_rx_task, higher_priority_task_woken);
    }
    return pdPASS;
}

BaseType_t VcuRtos_CanRxPop(VcuCanFrame_t *frame, TickType_t wait_ticks)
{
    if (frame == NULL || g_can_rx_queue == NULL) return pdFAIL;
    return xQueueReceive(g_can_rx_queue, frame, wait_ticks);
}

uint32_t VcuRtos_CanRxOverflowCount(void)
{
    return g_can_rx_overflow_count;
}

uint8_t VcuRtos_CanStarted(void) { return g_can_started; }
uint8_t VcuRtos_CanBusOff(void)
{
    return (g_can_bus_off_mask & 0x01U) ? 1U : 0U;
}

uint32_t VcuRtos_CanRawRxCount(uint8_t bus)
{
    return bus < 2U ? g_can_raw_rx_count[bus] : 0U;
}

uint8_t VcuRtos_CanBusOffActive(void)
{
    return (hcan1.Instance->ESR & CAN_ESR_BOFF) != 0U ? 1U : 0U;
}

void VcuRtos_GetDiagnostics(PatrolDiagnosticsSnapshot* snapshot)
{
    if (snapshot == NULL) return;
    taskENTER_CRITICAL();
    PatrolDiagnostics_Snapshot(&g_diagnostics, HAL_GetTick(), snapshot);
    taskEXIT_CRITICAL();
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)VcuRtos_CanRxFromISR(hcan, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (g_health_events != NULL && hcan != NULL && (HAL_CAN_GetError(hcan) & HAL_CAN_ERROR_BOF) != 0U)
    {
        g_can_bus_off_mask |= hcan->Instance == CAN1 ? 0x01U : 0x02U;
        if (hcan->Instance == CAN1)
            xEventGroupSetBitsFromISR(g_health_events, VCU_EVENT_CAN_ERROR, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static uint8_t bench_arm(uint16_t limit_mdeg, uint8_t drive)
{
    PatrolDiagnosticsSnapshot s;
    uint8_t result = 0;
    taskENTER_CRITICAL();
    PatrolDiagnostics_Snapshot(&g_diagnostics, HAL_GetTick(), &s);
    if (PATROL_CHASSIS_BENCH_STEERING_ENABLED && (!drive || PATROL_CHASSIS_BENCH_DRIVE_ENABLED) &&
        g_bench.phase == BENCH_IDLE && g_drive.phase == BENCH_IDLE &&
        g_coordinate.valid &&
        VcuConfig_Get()->calibrated && VcuConfig_CalibrationCompatible() &&
        VcuConfig_SingleTurnProfileReady() && RemoteService_State()->feedback.encoder_range_valid &&
        s.steering_feedback_fresh && s.drive_feedback_fresh && s.encoder_online &&
        s.steering_status_fresh && s.drive_status_fresh &&
        s.encoder_config_valid && !s.drive_fault && !OtaService_IsBusy() &&
        !VcuRtos_CanBusOff() && VehiclePowerOutput_IsOn() &&
        (s.steering_status_word & 0x6FU) != 0x27U && (s.drive_status_word & 4U) == 0 &&
        labs(s.left_actual_rpm) <= 20 && labs(s.right_actual_rpm) <= 20)
        result = drive ? RemoteService_BenchDriveArm(limit_mdeg, HAL_GetTick()) :
            RemoteService_BenchArm(limit_mdeg, HAL_GetTick());
    taskEXIT_CRITICAL();
    return result;
}
uint8_t VcuRtos_BenchArm(uint16_t limit) { return bench_arm(limit, 0); }
uint8_t VcuRtos_BenchDriveArm(uint16_t limit) { return bench_arm(limit, 1); }
void VcuRtos_BenchStop(void)
{
    taskENTER_CRITICAL();
    RemoteService_BenchRevoke();
    BenchSteering_Stop(&g_bench, HAL_GetTick(), 0);
    BenchDrive_Stop(&g_drive, HAL_GetTick(), 0);
    taskEXIT_CRITICAL();
}
void VcuRtos_FormatBench(char* out, uint16_t size)
{
    taskENTER_CRITICAL();
    snprintf(out, size, "{\"available\":%s,\"permitted\":%s,\"phase\":%u,\"step\":%u,"
        "\"fault\":%lu,\"feedbackGateMask\":%u,\"coordinateValid\":%s,\"runtimeCenterCount\":%ld,"
        "\"limitMdeg\":%u,\"sessionId\":\"%s\",\"driveAvailable\":%s,\"drivePermitted\":%s,\"drivePhase\":%u,\"driveFault\":%lu,\"driveLimitRpm\":200,"
        "\"safetyProfile\":\"single_turn_4096_center2048_limit30_v1\",\"profileReady\":%s,\"rangeValid\":%s,\"contactorFeedbackAvailable\":false}",
        PATROL_CHASSIS_BENCH_STEERING_ENABLED ? "true" : "false",
        RemoteService_BenchPermitted() ? "true" : "false", (unsigned)g_bench.phase,
        (unsigned)g_bench.step, (unsigned long)g_bench.fault,
        g_bench.phase == BENCH_FAULT ? g_bench_fault_gate_mask : g_feedback_gate_mask,
        g_coordinate.valid ? "true" : "false", (long)g_coordinate.runtime_center,
        RemoteService_BenchLimit(), RemoteService_BenchSession(),
        PATROL_CHASSIS_BENCH_DRIVE_ENABLED ? "true" : "false",
        RemoteService_BenchDrivePermitted() ? "true" : "false", (unsigned)g_drive.phase, (unsigned long)g_drive.fault,
        VcuConfig_SingleTurnProfileReady() ? "true" : "false",
        RemoteService_State()->feedback.encoder_range_valid ? "true" : "false");
    taskEXIT_CRITICAL();
}

static uint8_t center_feedback_ready(const PatrolDiagnosticsSnapshot* s, uint8_t require_calibration)
{
    const ChassisControl* c = RemoteService_State();
    if (s == NULL || c == NULL || !VehiclePowerOutput_IsOn() || VcuRtos_CanBusOff() ||
        VcuRtos_CanBusOffActive() || OtaService_IsBusy() || !s->steering_online ||
        !s->drive_online || !s->steering_feedback_fresh || !s->drive_feedback_fresh ||
        !s->steering_status_fresh || !s->drive_status_fresh || !s->encoder_online ||
        !s->encoder_config_valid || s->drive_fault || labs(s->left_actual_rpm) > 20L ||
        labs(s->right_actual_rpm) > 20L || RemoteService_BenchPermitted() || c->session_active ||
        c->enable_request || (s->drive_status_word & 0x006FU) == 0x0027U) return 0U;
    if (require_calibration && (!VcuConfig_Get()->calibrated || !VcuConfig_CalibrationCompatible() ||
        !g_coordinate.valid || !c->feedback.encoder_range_valid ||
        llabs((int64_t)c->feedback.steering_actual_mdeg - c->feedback.encoder_actual_mdeg) >
            VEHICLE_STEERING_TOLERANCE_MDEG)) return 0U;
    return 1U;
}

static void center_calibration_fail(uint32_t fault)
{
    taskENTER_CRITICAL();
    g_center_cal.phase = CENTER_CAL_FAILED;
    g_center_cal.fault = fault;
    g_center_cal.encoder_write_state = 0U;
    BenchSteering_Stop(&g_bench, HAL_GetTick(), 1U);
    SteeringCoordinate_Reset(&g_coordinate);
    taskEXIT_CRITICAL();
    RemoteService_BenchFault();
    (void)VehiclePowerOutput_Set(0U);
}

uint8_t VcuRtos_CenterCalibrationStart(void)
{
    PatrolDiagnosticsSnapshot s;
    const ChassisControl* c = RemoteService_State();
    uint32_t now = HAL_GetTick();
    VcuRtos_GetDiagnostics(&s);
    taskENTER_CRITICAL();
    if (g_center_cal.phase != CENTER_CAL_IDLE || g_bench.phase != BENCH_IDLE ||
        g_drive.phase != BENCH_IDLE || !center_feedback_ready(&s, 1U) ||
        labs(c->feedback.encoder_actual_mdeg) > VEHICLE_CENTER_JOG_LIMIT_MDEG ||
        labs(c->feedback.steering_actual_mdeg) > VEHICLE_CENTER_JOG_LIMIT_MDEG) {
        taskEXIT_CRITICAL();
        return 0U;
    }
    memset(&g_center_cal, 0, sizeof(g_center_cal));
    g_center_cal.serial = ++g_center_cal_serial;
    g_center_cal.phase = CENTER_CAL_START_STABLE;
    g_center_cal.start_mdeg = c->feedback.encoder_actual_mdeg;
    g_center_cal.target_mdeg = g_center_cal.start_mdeg;
    g_center_cal.deadline_ms = now + 7000U;
    (void)snprintf(g_center_cal.session_id, sizeof(g_center_cal.session_id), "c-%lu-%lu",
                   (unsigned long)now, (unsigned long)g_center_cal.serial);
    taskEXIT_CRITICAL();
    return 1U;
}

static uint8_t center_session_matches(const char* session_id)
{
    return session_id != NULL && session_id[0] != '\0' &&
        strcmp(session_id, g_center_cal.session_id) == 0;
}

uint8_t VcuRtos_CenterCalibrationKeepAlive(const char* session_id)
{
    uint8_t accepted;
    taskENTER_CRITICAL();
    accepted = g_center_cal.phase != CENTER_CAL_IDLE && center_session_matches(session_id);
    if (accepted && g_center_cal.phase == CENTER_CAL_READY)
        g_center_cal.deadline_ms = HAL_GetTick() + 4000U;
    taskEXIT_CRITICAL();
    return accepted;
}

uint8_t VcuRtos_CenterCalibrationJog(const char* session_id, int16_t delta_mdeg)
{
    PatrolDiagnosticsSnapshot s;
    int32_t next;
    VcuRtos_GetDiagnostics(&s);
    taskENTER_CRITICAL();
    next = g_center_cal.accumulated_mdeg + delta_mdeg;
    if (g_center_cal.phase != CENTER_CAL_READY || !center_session_matches(session_id) ||
        (delta_mdeg != VEHICLE_CENTER_JOG_FINE_MDEG && delta_mdeg != -VEHICLE_CENTER_JOG_FINE_MDEG &&
         delta_mdeg != VEHICLE_CENTER_JOG_COARSE_MDEG && delta_mdeg != -VEHICLE_CENTER_JOG_COARSE_MDEG) ||
        labs(next) > VEHICLE_CENTER_JOG_LIMIT_MDEG || !center_feedback_ready(&s, 1U)) {
        taskEXIT_CRITICAL();
        return 0U;
    }
    g_center_cal.accumulated_mdeg = next;
    g_center_cal.target_mdeg = g_center_cal.start_mdeg + next;
    g_center_cal.stable_samples = 0U;
    g_center_cal.last_sample_ms = 0U;
    g_center_cal.deadline_ms = HAL_GetTick() + 7000U;
    g_center_cal.phase = CENTER_CAL_JOGGING;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t VcuRtos_CenterCalibrationCommit(const char* session_id)
{
    if (g_center_cal.phase != CENTER_CAL_READY || !center_session_matches(session_id)) return 0U;
    taskENTER_CRITICAL();
    g_center_cal.phase = CENTER_CAL_COMMIT_STABLE;
    g_center_cal.stable_samples = 0U;
    g_center_cal.last_sample_ms = 0U;
    g_center_cal.deadline_ms = HAL_GetTick() + 7000U;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t VcuRtos_CenterCalibrationCancel(const char* session_id)
{
    if (g_center_cal.phase == CENTER_CAL_IDLE || !center_session_matches(session_id)) return 0U;
    taskENTER_CRITICAL();
    BenchSteering_Stop(&g_bench, HAL_GetTick(), 0U);
    memset(&g_center_cal, 0, sizeof(g_center_cal));
    taskEXIT_CRITICAL();
    RemoteService_BenchRevoke();
    (void)VehiclePowerOutput_Set(0U);
    return 1U;
}

void VcuRtos_FormatCenterCalibration(char* out, uint16_t size)
{
    PatrolDiagnosticsSnapshot s;
    const ChassisControl* c = RemoteService_State();
    VcuRtos_GetDiagnostics(&s);
    taskENTER_CRITICAL();
    (void)snprintf(out, size,
        "{\"phase\":%u,\"sessionId\":\"%s\",\"fault\":%lu,\"driverRaw\":%ld,"
        "\"encoderRaw\":%lu,\"targetMdeg\":%ld,\"jogOffsetMdeg\":%ld,"
        "\"profileReady\":%s,\"rangeValid\":%s,\"calibrated\":%s}",
        (unsigned)g_center_cal.phase, g_center_cal.session_id, (unsigned long)g_center_cal.fault,
        (long)s.steering_position_count, (unsigned long)s.encoder_count,
        (long)g_center_cal.target_mdeg, (long)g_center_cal.accumulated_mdeg,
        VcuConfig_SingleTurnProfileReady() ? "true" : "false",
        c->feedback.encoder_range_valid ? "true" : "false",
        VcuConfig_Get()->calibrated ? "true" : "false");
    taskEXIT_CRITICAL();
}

static void center_calibration_tick(uint32_t now)
{
    PatrolDiagnosticsSnapshot s;
    CenterCalibrationPhase phase = g_center_cal.phase;
    if (phase == CENTER_CAL_IDLE || phase == CENTER_CAL_COMPLETE || phase == CENTER_CAL_FAILED) return;
    if ((int32_t)(now - g_center_cal.deadline_ms) >= 0) { center_calibration_fail(1U); return; }
    VcuRtos_GetDiagnostics(&s);
    if (phase == CENTER_CAL_START_STABLE) {
        if (!center_feedback_ready(&s, 1U)) { center_calibration_fail(2U); return; }
        if (s.coherent_feedback_ms == 0U || s.coherent_feedback_ms == g_center_cal.last_sample_ms) return;
        g_center_cal.last_sample_ms = s.coherent_feedback_ms;
        if (++g_center_cal.stable_samples < 3U) return;
        taskENTER_CRITICAL();
        g_center_cal.phase = CENTER_CAL_READY;
        g_center_cal.deadline_ms = now + 4000U;
        taskEXIT_CRITICAL();
        return;
    }
    if (phase == CENTER_CAL_JOGGING || phase == CENTER_CAL_STOPPING) {
        if (!center_feedback_ready(&s, 1U) || g_bench.phase == BENCH_FAULT) {
            center_calibration_fail(2U); return;
        }
        if (phase == CENTER_CAL_STOPPING && g_bench.phase == BENCH_IDLE) {
            taskENTER_CRITICAL();
            g_center_cal.phase = CENTER_CAL_READY;
            g_center_cal.deadline_ms = now + 4000U;
            taskEXIT_CRITICAL();
        }
        return;
    }
    if (phase == CENTER_CAL_COMMIT_STABLE) {
        if (g_bench.phase != BENCH_IDLE || !center_feedback_ready(&s, 0U) ||
            (s.steering_status_word & 0x006FU) == 0x0027U) { center_calibration_fail(3U); return; }
        if (s.coherent_feedback_ms == 0U || s.coherent_feedback_ms == g_center_cal.last_sample_ms) return;
        g_center_cal.last_sample_ms = s.coherent_feedback_ms;
        if (++g_center_cal.stable_samples < 3U) return;
        g_center_cal.captured_driver_count = s.steering_position_count;
        if (!VcuConfig_ClearCalibration()) { center_calibration_fail(4U); return; }
        taskENTER_CRITICAL();
        SteeringCoordinate_Reset(&g_coordinate);
        g_center_cal.encoder_write_state = 1U;
        g_center_cal.phase = CENTER_CAL_ENCODER_WRITE;
        g_center_cal.deadline_ms = now + 2000U;
        taskEXIT_CRITICAL();
        return;
    }
    if (phase == CENTER_CAL_ENCODER_WRITE) {
        if (g_center_cal.encoder_write_state == 3U) { center_calibration_fail(5U); return; }
        if (g_center_cal.encoder_write_state == 2U || g_center_cal.encoder_write_state == 5U) {
            taskENTER_CRITICAL();
            g_center_cal.phase = CENTER_CAL_ENCODER_VERIFY;
            g_center_cal.stable_samples = 0U;
            g_center_cal.last_sample_ms = s.encoder_sample_ms;
            g_center_cal.deadline_ms = now + 2000U;
            taskEXIT_CRITICAL();
        }
        return;
    }
    if (phase == CENTER_CAL_ENCODER_VERIFY) {
        if (s.encoder_sample_ms == 0U || s.encoder_sample_ms == g_center_cal.last_sample_ms) return;
        g_center_cal.last_sample_ms = s.encoder_sample_ms;
        if (s.encoder_count < VEHICLE_ABSOLUTE_CENTER_COUNT - 2U ||
            s.encoder_count > VEHICLE_ABSOLUTE_CENTER_COUNT + 2U) { center_calibration_fail(6U); return; }
        if (++g_center_cal.stable_samples < 3U) return;
        if (!VcuConfig_SetCalibration(g_center_cal.captured_driver_count,
                VEHICLE_ABSOLUTE_CENTER_COUNT, VEHICLE_STEERING_COUNTS_MILLI,
                VEHICLE_ABSOLUTE_COUNTS_MILLI, VEHICLE_STEERING_DIRECTION,
                VEHICLE_ABSOLUTE_DIRECTION, VEHICLE_STEERING_TOLERANCE_MDEG)) {
            center_calibration_fail(7U); return;
        }
        RemoteService_BenchRevoke();
        (void)VehiclePowerOutput_Set(0U);
        taskENTER_CRITICAL();
        SteeringCoordinate_Reset(&g_coordinate);
        g_center_cal.phase = CENTER_CAL_COMPLETE;
        g_center_cal.deadline_ms = now + 60000U;
        taskEXIT_CRITICAL();
    }
}

static void Vcu_SafetyControlTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;)
    {
        taskENTER_CRITICAL();
        RemoteService_SafetyTick(HAL_GetTick());
        taskEXIT_CRITICAL();
        xEventGroupSetBits(g_health_events, VCU_HEALTH_SAFETY);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

static void Vcu_CanRxTask(void *argument)
{
    VcuCanFrame_t frame;
    PatrolCanFrame decoded;
    (void)argument;
    vcu_start_can();
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
        while (VcuRtos_CanRxPop(&frame, 0U) == pdPASS)
        {
            if (frame.bus != 0U || frame.header.IDE != CAN_ID_STD || frame.header.RTR != CAN_RTR_DATA)
                continue;
            memset(&decoded, 0, sizeof(decoded));
            decoded.id = (uint16_t)frame.header.StdId;
            decoded.dlc = (uint8_t)frame.header.DLC;
            memcpy(decoded.data, frame.data, sizeof(decoded.data));
            taskENTER_CRITICAL();
            /* Exactly one SDO owner; executor responses never satisfy a diagnostic request. */
            if ((decoded.id == 0x701 || decoded.id == 0x702) && decoded.dlc == 1 && decoded.data[0] == 0 && g_coordinate.valid)
                g_coordinate_reset_requested = 1;
            uint8_t execution = BenchSteering_OnFrame(&g_bench, &decoded, frame.timestamp_ms) ? 1 :
                BenchDrive_OnFrame(&g_drive, &decoded, frame.timestamp_ms) ? 2 : 0;
            if (!execution)
                PatrolDiagnostics_OnCanFrame(&g_diagnostics, &decoded, frame.timestamp_ms);
            else {
                int32_t status; uint8_t width;
                g_diagnostics.can_rx_count++;
                /* An executor-owned validated status upload is also fresh feedback;
                 * it must not complete a diagnostics transaction or advance its cursor. */
                if ((execution == 1 ? g_bench.phase : g_drive.phase) != BENCH_FAULT &&
                    PatrolCanopen_ParseSdoValue(&decoded, execution, 0x6041, 0, &status, &width) && width == 2) {
                    if (execution == 1) {
                        g_diagnostics.steering_status_word = (uint16_t)status;
                        g_diagnostics.steering_status_ms = frame.timestamp_ms;
                    } else {
                        g_diagnostics.drive_status_word = (uint16_t)status;
                        g_diagnostics.drive_status_ms = frame.timestamp_ms;
                    }
                }
            }
            taskEXIT_CRITICAL();
        }
        xEventGroupSetBits(g_health_events, VCU_HEALTH_CAN_RX);
    }
}

static void Vcu_CanopenTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
#if PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
    uint32_t feedback_due = 0U;
#endif
    (void)argument;
    for (;;)
    {
#if PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
        PatrolCanFrame request;
        uint8_t have_request = 0U;
        uint32_t now;
        uint8_t bench_frame = 0U;
        taskENTER_CRITICAL();
        now = HAL_GetTick();
        {
            const ChassisControl* c = RemoteService_State();
            const VcuRemoteConfig_t* cfg = VcuConfig_Get();
            if (!VehiclePowerOutput_IsOn()) {
                SteeringCoordinate_Reset(&g_coordinate);
                g_bench.limits_aligned = 0;
            }
            if (g_coordinate_reset_requested) {
                BenchSteering_Stop(&g_bench, now, 1);
                SteeringCoordinate_Reset(&g_coordinate);
                g_coordinate_reset_requested = 0;
            }
            uint8_t safe = g_coordinate.valid && VehiclePowerOutput_IsOn() && !VcuRtos_CanBusOffActive() &&
                c->feedback.power_ready && c->feedback.calibrated &&
                c->feedback.steering_online && c->feedback.drive_online &&
                !c->feedback.drive_fault && !c->feedback.can_bus_off && !OtaService_IsBusy() &&
                now - c->feedback.feedback_ms <= CHASSIS_FEEDBACK_TIMEOUT_MS &&
                llabs((int64_t)c->feedback.steering_actual_mdeg - c->feedback.encoder_actual_mdeg) <= 2000;
            uint8_t remote_requested = RemoteService_BenchPermitted() && c->session_active && c->enable_request;
            uint8_t center_requested = g_center_cal.phase == CENTER_CAL_JOGGING;
            uint8_t requested = remote_requested || center_requested;
            int32_t requested_angle = center_requested ? g_center_cal.target_mdeg : c->target_steering_mdeg;
            if (PATROL_CHASSIS_BENCH_STEERING_ENABLED && requested && safe && g_bench.phase == BENCH_IDLE)
                BenchSteering_Start(&g_bench, g_diagnostics.steering_position_count, g_coordinate.runtime_center,
                    cfg->steering_center_count,
                    center_requested ? VEHICLE_MAX_STEERING_MDEG : RemoteService_BenchLimit(), now);
            safe = safe && !c->fault_latched;
            if (PATROL_CHASSIS_BENCH_DRIVE_ENABLED && remote_requested && safe &&
                RemoteService_BenchDrivePermitted() && g_bench.phase == BENCH_ACTIVE && g_drive.phase == BENCH_IDLE)
                BenchDrive_Start(&g_drive, now);
            BenchDrive_Tick(&g_drive, now, remote_requested && RemoteService_BenchDrivePermitted(), safe,
                c->target_rpm, g_diagnostics.left_actual_rpm, g_diagnostics.right_actual_rpm,
                g_diagnostics.left_rpm_ms, g_diagnostics.right_rpm_ms);
            BenchSteering_Tick(&g_bench, now, requested && g_drive.phase != BENCH_FAULT, safe, requested_angle);
            g_diagnostics.motion_priority = (g_bench.phase != BENCH_IDLE && g_bench.phase != BENCH_FAULT) ||
                (g_drive.phase != BENCH_IDLE && g_drive.phase != BENCH_FAULT);
            if (g_bench.phase == BENCH_STOPPING || g_bench.phase == BENCH_FAULT) {
                RemoteService_BenchRevoke();
                if (!g_bench_transport_stopped) {
                    g_diagnostics.pending_valid = 0;
                    HAL_CAN_AbortTxRequest(&hcan1, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
                    g_bench_transport_stopped = 1;
                }
            } else g_bench_transport_stopped = 0;
        }
        taskEXIT_CRITICAL();
        if (g_bench.cut_power || g_drive.cut_power) {
            BenchDrive_Stop(&g_drive, now, 1);
            BenchSteering_Stop(&g_bench, now, 1);
            g_bench_fault_gate_mask = g_feedback_gate_mask;
            RemoteService_BenchFault();
            (void)VehiclePowerOutput_Set(0);
            g_bench.cut_power = 0; g_drive.cut_power = 0;
        }
        if (VehiclePowerOutput_IsOn() && !VcuRtos_CanBusOffActive() &&
            HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U)
        {
            taskENTER_CRITICAL();
            /* Alternate execution and diagnostics, never overlap pending transactions. */
            if (!g_diagnostics.pending_valid && !g_bench.pending && !g_drive.pending &&
                (g_bench_turn || g_bench.phase == BENCH_STOPPING)) {
                if (g_drive_turn && g_bench.phase != BENCH_STOPPING) {
                    have_request = BenchDrive_Take(&g_drive, now, &request);
                    if (have_request) { bench_frame = 2; g_drive_turn = 0; }
                }
                if (!have_request) {
                    have_request = BenchSteering_Take(&g_bench, now, &request);
                    if (have_request) { bench_frame = 1; g_drive_turn = 1; }
                }
                if (!have_request) {
                    have_request = BenchDrive_Take(&g_drive, now, &request);
                    if (have_request) { bench_frame = 2; g_drive_turn = 0; }
                }
                if (have_request) g_bench_turn = 0;
            }
            if (!have_request && !g_bench.pending && !g_drive.pending && g_bench.phase != BENCH_STOPPING) {
                have_request = PatrolDiagnostics_TakeSdoRead(&g_diagnostics, now, &request);
                if (have_request) g_bench_turn = 1;
            }
            if (!have_request && !g_diagnostics.pending_valid && !g_bench.pending && !g_drive.pending) {
                if (g_drive_turn && g_bench.phase != BENCH_STOPPING) {
                    have_request = BenchDrive_Take(&g_drive, now, &request);
                    if (have_request) { bench_frame = 2; g_drive_turn = 0; }
                }
                if (!have_request) {
                    have_request = BenchSteering_Take(&g_bench, now, &request);
                    if (have_request) { bench_frame = 1; g_drive_turn = 1; }
                }
                if (!have_request) {
                    have_request = BenchDrive_Take(&g_drive, now, &request);
                    if (have_request) { bench_frame = 2; g_drive_turn = 0; }
                }
            }
            taskEXIT_CRITICAL();
        }
        if (have_request)
        {
            CAN_TxHeaderTypeDef header;
            uint32_t mailbox;
            memset(&header, 0, sizeof(header));
            header.StdId = request.id;
            header.IDE = CAN_ID_STD;
            header.RTR = CAN_RTR_DATA;
            header.DLC = request.dlc;
            header.TransmitGlobalTime = DISABLE;
            taskENTER_CRITICAL();
            /* A higher priority safety tick may have revoked the selected command. */
            if (bench_frame && g_center_cal.phase != CENTER_CAL_JOGGING &&
                g_center_cal.phase != CENTER_CAL_STOPPING && (bench_frame == 1 ?
                (g_bench.phase != BENCH_STOPPING && g_bench.phase != BENCH_IDLE) :
                (g_drive.phase != BENCH_STOPPING && g_drive.phase != BENCH_IDLE)) &&
                (!RemoteService_BenchPermitted() || !RemoteService_State()->enable_request ||
                 (int32_t)(HAL_GetTick() - RemoteService_State()->lease_deadline_ms) >= 0)) {
                BenchSteering_Stop(&g_bench, HAL_GetTick(), 0);
                BenchDrive_Stop(&g_drive, HAL_GetTick(), 0);
                have_request = 0;
            }
            /* Each compiled executor is restricted to its own node. */
            if (have_request && ((bench_frame == 1 && PATROL_CHASSIS_BENCH_STEERING_ENABLED &&
                    (request.id == 0x601U || (request.id == 0 && request.data[1] == 1))) ||
                 (bench_frame == 2 && PATROL_CHASSIS_BENCH_DRIVE_ENABLED &&
                    (request.id == 0x602U || (request.id == 0 && request.data[1] == 2))) ||
                 (!bench_frame && (request.id == 0x601U || request.id == 0x602U) && request.data[0] == 0x40U)) &&
                HAL_CAN_AddTxMessage(&hcan1, &header, request.data, &mailbox) != HAL_OK)
            {
                taskENTER_CRITICAL();
                if (bench_frame == 2) BenchDrive_TxFailed(&g_drive);
                else if (bench_frame == 1) BenchSteering_TxFailed(&g_bench);
                else PatrolDiagnostics_OnSdoTxError(&g_diagnostics, now);
                taskEXIT_CRITICAL();
            }
            taskEXIT_CRITICAL();
        }
        if ((int32_t)(now - feedback_due) >= 0)
        {
            PatrolDiagnosticsSnapshot snapshot;
            ChassisFeedback feedback;
            const VcuRemoteConfig_t* cfg = VcuConfig_Get();
            memset(&feedback, 0, sizeof(feedback));
            taskENTER_CRITICAL();
            /* Read time and feedback under the same lock: RX can otherwise publish a
             * timestamp newer than now and unsigned age arithmetic reports it stale. */
            now = HAL_GetTick();
            PatrolDiagnostics_Snapshot(&g_diagnostics, now, &snapshot);
            taskEXIT_CRITICAL();
            feedback.calibrated = cfg != NULL && cfg->calibrated && VcuConfig_CalibrationCompatible();
            feedback.encoder_profile_ready = VcuConfig_SingleTurnProfileReady();
            feedback.steering_online = snapshot.steering_online;
            feedback.drive_online = snapshot.drive_online;
            feedback.can_bus_off = VcuRtos_CanBusOff();
            feedback.drive_fault = snapshot.drive_fault;
            feedback.ota_busy = OtaService_IsBusy();
            /* Bench readiness is not physical contactor feedback. No unconfirmed pin is read. */
            g_feedback_gate_mask = (!snapshot.steering_feedback_fresh ? 1U : 0U) |
                (!snapshot.drive_feedback_fresh ? 2U : 0U) | (!snapshot.encoder_online ? 4U : 0U) |
                (!snapshot.steering_status_fresh ? 8U : 0U) | (!snapshot.drive_status_fresh ? 16U : 0U) |
                (!snapshot.encoder_config_valid ? 32U : 0U) |
                ((snapshot.drive_status_word & 4U) != 0 ? 64U : 0U) |
                ((labs(snapshot.left_actual_rpm) > 20 || labs(snapshot.right_actual_rpm) > 20) ? 128U : 0U) |
                (!VehiclePowerOutput_IsOn() ? 256U : 0U);
            /* Only an executing drive session may pass moving/Operation Enabled gates.
             * Keep this allowance during bounded stopping after RAM permission is revoked. */
            if (g_drive.phase != BENCH_IDLE && g_drive.phase != BENCH_FAULT)
                g_feedback_gate_mask &= (uint16_t)~(64U | 128U);
            feedback.encoder_range_valid = SteeringAngle_AbsoluteWithinLimit(
                snapshot.encoder_count, cfg->absolute_center_count,
                cfg->absolute_counts_per_degree_milli, cfg->absolute_direction,
                VEHICLE_MAX_STEERING_MDEG, &feedback.encoder_actual_mdeg);
            if (PATROL_CHASSIS_BENCH_STEERING_ENABLED) {
                uint8_t was_valid = g_coordinate.valid;
                if (!VehiclePowerOutput_IsOn()) SteeringCoordinate_Reset(&g_coordinate);
                else if (g_coordinate.valid && (g_feedback_gate_mask != 0 ||
                    !snapshot.steering_online || !snapshot.drive_online ||
                    cfg->steering_center_count != g_coordinate_calibration_center ||
                    cfg->absolute_center_count != g_coordinate_absolute_center)) {
                    /* A lost position epoch requires power-off, never a live re-zero. */
                    taskENTER_CRITICAL();
                    BenchSteering_Stop(&g_bench, now, 1);
                    SteeringCoordinate_Reset(&g_coordinate);
                    taskEXIT_CRITICAL();
                } else {
                    SteeringCoordinate_Observe(&g_coordinate,
                        g_feedback_gate_mask == 0 && feedback.calibrated && feedback.encoder_range_valid &&
                        snapshot.steering_online && snapshot.drive_online &&
                        !snapshot.drive_fault && !feedback.can_bus_off && !feedback.ota_busy &&
                        (snapshot.steering_status_word & 0x6FU) != 0x27U,
                        snapshot.steering_position_count, snapshot.encoder_count, cfg->absolute_center_count,
                        snapshot.coherent_feedback_ms, now);
                }
                if (!was_valid && g_coordinate.valid) {
                    g_coordinate_calibration_center = cfg->steering_center_count;
                    g_coordinate_absolute_center = cfg->absolute_center_count;
                }
                if (!g_coordinate.valid) g_feedback_gate_mask |= 512U;
            }
            feedback.power_ready = PATROL_CHASSIS_BENCH_STEERING_ENABLED && g_feedback_gate_mask == 0;
            feedback.left_actual_rpm = snapshot.left_actual_rpm > 32767 ? 32767 :
                (snapshot.left_actual_rpm < -32768 ? -32768 : (int16_t)snapshot.left_actual_rpm);
            /* Normalize mirror-mounted right-wheel feedback to vehicle coordinates.
             * Raw controller sign remains available in get_can_diag. */
            feedback.right_actual_rpm = snapshot.right_actual_rpm > 32768 ? -32768 :
                (snapshot.right_actual_rpm < -32767 ? 32767 : (int16_t)-snapshot.right_actual_rpm);
            if (feedback.calibrated)
            {
                feedback.steering_actual_mdeg = SteeringAngle_FromControllerCount(
                    snapshot.steering_position_count,
                    PATROL_CHASSIS_BENCH_STEERING_ENABLED && g_coordinate.valid ?
                        g_coordinate.runtime_center : cfg->steering_center_count,
                    cfg->steering_counts_per_degree_milli, cfg->steering_direction);
                if (!feedback.encoder_range_valid)
                    feedback.encoder_actual_mdeg = SteeringAngle_FromAbsoluteCount(
                        snapshot.encoder_count, cfg->absolute_center_count,
                        cfg->absolute_counts_per_degree_milli, cfg->absolute_direction);
            }
            feedback.feedback_ms = snapshot.coherent_feedback_ms != 0U ?
                snapshot.coherent_feedback_ms : now - (CHASSIS_FEEDBACK_TIMEOUT_MS + 1U);
            taskENTER_CRITICAL();
            RemoteService_UpdateFeedback(&feedback);
            if (g_center_cal.phase == CENTER_CAL_JOGGING && g_bench.phase == BENCH_ACTIVE &&
                snapshot.coherent_feedback_ms != 0U &&
                snapshot.coherent_feedback_ms != g_center_cal.last_sample_ms &&
                labs(feedback.steering_actual_mdeg - g_center_cal.target_mdeg) <= 200L &&
                labs(feedback.encoder_actual_mdeg - g_center_cal.target_mdeg) <= 200L) {
                g_center_cal.last_sample_ms = snapshot.coherent_feedback_ms;
                if (++g_center_cal.stable_samples >= 3U) {
                    BenchSteering_Stop(&g_bench, now, 0U);
                    g_center_cal.phase = CENTER_CAL_STOPPING;
                    g_center_cal.deadline_ms = now + 2000U;
                }
            }
            taskEXIT_CRITICAL();
            feedback_due = now + 20U;
        }
#endif
        xEventGroupSetBits(g_health_events, VCU_HEALTH_CANOPEN);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

static void Vcu_EncoderTask(void *argument)
{
    (void)argument;
    for (;;)
    {
#if PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
        uint8_t request[ABS_ENCODER_REQUEST_SIZE];
        uint8_t response[16] = {0};
        uint16_t registers[3];
        uint16_t response_length = 0U;
        uint16_t register_count;
        uint8_t read_config;
        HAL_StatusTypeDef status;
        PatrolDiagnosticsSnapshot snapshot;
        uint32_t now;
        uint8_t write_center = 0U;
        taskENTER_CRITICAL();
        now = HAL_GetTick();
        PatrolDiagnostics_Snapshot(&g_diagnostics, now, &snapshot);
        if (g_center_cal.encoder_write_state == 1U) {
            g_center_cal.encoder_write_state = 4U;
            write_center = 1U;
        }
        taskEXIT_CRITICAL();
        if (write_center) {
            AbsoluteEncoder_BuildWriteSingleRequest(3U, VEHICLE_ABSOLUTE_CENTER_COUNT, request);
            (void)HAL_UART_AbortReceive(&huart5);
            __HAL_UART_FLUSH_DRREGISTER(&huart5);
            status = HAL_UART_Transmit(&huart5, request, sizeof(request), 30U);
            if (status == HAL_OK) {
                HAL_StatusTypeDef receive_status = HAL_UARTEx_ReceiveToIdle(
                    &huart5, response, sizeof(response), &response_length, 40U);
                taskENTER_CRITICAL();
                g_center_cal.encoder_write_state = (receive_status == HAL_OK &&
                    AbsoluteEncoder_ParseWriteSingleResponse(response, response_length, 3U,
                        VEHICLE_ABSOLUTE_CENTER_COUNT)) ? 2U : 5U;
                taskEXIT_CRITICAL();
            } else {
                taskENTER_CRITICAL();
                g_center_cal.encoder_write_state = 3U;
                taskEXIT_CRITICAL();
            }
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }
        read_config = (uint8_t)(!snapshot.encoder_config_valid);
        register_count = read_config ? 3U : 2U;
        AbsoluteEncoder_BuildReadHoldingRequest(read_config ? 0U : 3U, register_count, request);
        (void)HAL_UART_AbortReceive(&huart5);
        __HAL_UART_FLUSH_DRREGISTER(&huart5);
        status = HAL_UART_Transmit(&huart5, request, sizeof(request), 30U);
        if (status == HAL_OK)
            status = HAL_UARTEx_ReceiveToIdle(&huart5, response, sizeof(response), &response_length, 40U);
        taskENTER_CRITICAL();
        if (status == HAL_OK && AbsoluteEncoder_ParseHoldingResponse(response, response_length,
                                                                     registers, register_count))
        {
            if (read_config)
                PatrolDiagnostics_OnEncoderConfig(&g_diagnostics, registers, now);
            else
                PatrolDiagnostics_OnEncoderCount(&g_diagnostics,
                    (uint32_t)registers[0] | (uint32_t)registers[1] << 16, now);
        }
        else if (response_length >= 2U && response[0] == ABS_ENCODER_ADDRESS &&
                 response[1] == (uint8_t)(0x03U | 0x80U))
            PatrolDiagnostics_OnEncoderError(&g_diagnostics, PATROL_ENCODER_ERROR_EXCEPTION);
        else if (status == HAL_TIMEOUT || response_length == 0U)
            PatrolDiagnostics_OnEncoderError(&g_diagnostics, PATROL_ENCODER_ERROR_TIMEOUT);
        else if (response_length >= 5U && AbsoluteEncoder_Crc16(response,
                 (uint16_t)(response_length - 2U)) != ((uint16_t)response[response_length - 2U] |
                 (uint16_t)response[response_length - 1U] << 8))
            PatrolDiagnostics_OnEncoderError(&g_diagnostics, PATROL_ENCODER_ERROR_CRC);
        else if (status != HAL_OK)
            PatrolDiagnostics_OnEncoderError(&g_diagnostics, PATROL_ENCODER_ERROR_TRANSPORT);
        else
            PatrolDiagnostics_OnEncoderError(&g_diagnostics, PATROL_ENCODER_ERROR_FORMAT);
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(read_config ? 500U : 100U));
#else
        vTaskDelay(pdMS_TO_TICKS(1000U));
#endif
    }
}

static void Vcu_PowerTask(void *argument)
{
    ChassisPower power;
    (void)argument;
    ChassisPower_Init(&power, 0U, HAL_GetTick());
    for (;;)
    {
        /* hardware_available=0 prevents relay and enable requests on unknown pins. */
        ChassisPower_Tick(&power, HAL_GetTick(), 0U, 1U);
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void Vcu_RemoteTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        CloudService_Task();
        RemoteService_Task();
        center_calibration_tick(HAL_GetTick());
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void Vcu_DiagTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        AppLog_DebugTask();
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

static void Vcu_WatchdogTask(void *argument)
{
    const EventBits_t required = VCU_HEALTH_REQUIRED;
    const TickType_t trial_started = xTaskGetTickCount();
    (void)argument;
    for (;;)
    {
#ifdef PATROL_CHASSIS_APP_FAULT_INJECTION_TEST
        if (AppTest_WatchdogStep() != 0U) {
            for (;;) vTaskDelay(pdMS_TO_TICKS(100U));
        }
#endif
        EventBits_t healthy = xEventGroupWaitBits(g_health_events, required, pdTRUE, pdTRUE,
                                                  pdMS_TO_TICKS(500U));
        if ((healthy & required) == required)
        {
            (void)HAL_IWDG_Refresh(&hiwdg);
            /* Do not require MQTT, 4G, or a vehicle CAN peer for trial success. */
            if (g_boot_confirmed == 0U && (xTaskGetTickCount() - trial_started) >= pdMS_TO_TICKS(30000U) &&
                (xEventGroupGetBits(g_health_events) & VCU_EVENT_CAN_ERROR) == 0U)
            {
                g_boot_confirmed = BootState_Confirm();
            }
        }
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    PatrolFault_Record(PATROL_FAULT_STACK_OVERFLOW, 0U, 0U);
    NVIC_SystemReset();
    for (;;) {}
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    PatrolFault_Record(PATROL_FAULT_MALLOC_FAILED, 0U, 0U);
    NVIC_SystemReset();
    for (;;) {}
}
