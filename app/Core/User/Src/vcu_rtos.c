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
#include <stdlib.h>
#include "task.h"
#include "usart.h"
#include "vcu_config.h"
#include "vehicle_power_output.h"

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
static uint8_t g_bench_turn;
static uint8_t g_bench_transport_stopped;
static uint16_t g_feedback_gate_mask, g_bench_fault_gate_mask;
static SteeringCoordinate g_coordinate;
static uint8_t g_coordinate_reset_requested;
static int32_t g_coordinate_calibration_center;
static uint32_t g_coordinate_absolute_center;
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

uint8_t VcuRtos_BenchArm(uint16_t limit_mdeg)
{
    PatrolDiagnosticsSnapshot s;
    uint8_t result = 0;
    taskENTER_CRITICAL();
    PatrolDiagnostics_Snapshot(&g_diagnostics, HAL_GetTick(), &s);
    if (PATROL_CHASSIS_BENCH_STEERING_ENABLED && g_bench.phase == BENCH_IDLE &&
        g_coordinate.valid &&
        VcuConfig_Get()->calibrated && VcuConfig_CalibrationCompatible() &&
        s.steering_feedback_fresh && s.drive_feedback_fresh && s.encoder_online &&
        s.steering_status_fresh && s.drive_status_fresh &&
        s.encoder_config_valid && !s.drive_fault && !OtaService_IsBusy() &&
        !VcuRtos_CanBusOff() && VehiclePowerOutput_IsOn() &&
        (s.steering_status_word & 0x6FU) != 0x27U && (s.drive_status_word & 0x6FU) != 0x27U &&
        labs(s.left_actual_rpm) <= 20 && labs(s.right_actual_rpm) <= 20)
        result = RemoteService_BenchArm(limit_mdeg, HAL_GetTick());
    taskEXIT_CRITICAL();
    return result;
}
void VcuRtos_BenchStop(void)
{
    taskENTER_CRITICAL();
    RemoteService_BenchRevoke();
    BenchSteering_Stop(&g_bench, HAL_GetTick(), 0);
    taskEXIT_CRITICAL();
}
void VcuRtos_FormatBench(char* out, uint16_t size)
{
    taskENTER_CRITICAL();
    snprintf(out, size, "{\"available\":%s,\"permitted\":%s,\"phase\":%u,\"step\":%u,"
        "\"fault\":%lu,\"feedbackGateMask\":%u,\"coordinateValid\":%s,\"runtimeCenterCount\":%ld,"
        "\"limitMdeg\":%u,\"sessionId\":\"%s\",\"contactorFeedbackAvailable\":false}",
        PATROL_CHASSIS_BENCH_STEERING_ENABLED ? "true" : "false",
        RemoteService_BenchPermitted() ? "true" : "false", (unsigned)g_bench.phase,
        (unsigned)g_bench.step, (unsigned long)g_bench.fault,
        g_bench.phase == BENCH_FAULT ? g_bench_fault_gate_mask : g_feedback_gate_mask,
        g_coordinate.valid ? "true" : "false", (long)g_coordinate.runtime_center,
        RemoteService_BenchLimit(), RemoteService_BenchSession());
    taskEXIT_CRITICAL();
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
            if (decoded.id == 0x701 && decoded.dlc == 1 && decoded.data[0] == 0 && g_coordinate.valid)
                g_coordinate_reset_requested = 1;
            if (!BenchSteering_OnFrame(&g_bench, &decoded, frame.timestamp_ms))
                PatrolDiagnostics_OnCanFrame(&g_diagnostics, &decoded, frame.timestamp_ms);
            else g_diagnostics.can_rx_count++;
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
            uint8_t requested = RemoteService_BenchPermitted() && c->session_active && c->enable_request;
            if (PATROL_CHASSIS_BENCH_STEERING_ENABLED && requested && safe && g_bench.phase == BENCH_IDLE)
                BenchSteering_Start(&g_bench, g_diagnostics.steering_position_count, g_coordinate.runtime_center,
                    cfg->steering_center_count,
                    RemoteService_BenchLimit(), now);
            BenchSteering_Tick(&g_bench, now, requested, safe, c->target_steering_mdeg);
            g_diagnostics.motion_priority = g_bench.phase != BENCH_IDLE && g_bench.phase != BENCH_FAULT;
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
        if (g_bench.cut_power) {
            g_bench_fault_gate_mask = g_feedback_gate_mask;
            RemoteService_BenchFault();
            (void)VehiclePowerOutput_Set(0);
            g_bench.cut_power = 0;
        }
        if (VehiclePowerOutput_IsOn() && !VcuRtos_CanBusOffActive() &&
            HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U)
        {
            taskENTER_CRITICAL();
            /* Alternate execution and diagnostics, never overlap pending transactions. */
            if (!g_diagnostics.pending_valid && !g_bench.pending &&
                (g_bench_turn || g_bench.phase == BENCH_STOPPING)) {
                have_request = BenchSteering_Take(&g_bench, now, &request);
                bench_frame = have_request;
                if (have_request) g_bench_turn = 0;
            }
            if (!have_request && !g_bench.pending && g_bench.phase != BENCH_STOPPING) {
                have_request = PatrolDiagnostics_TakeSdoRead(&g_diagnostics, now, &request);
                if (have_request) g_bench_turn = 1;
            }
            if (!have_request && !g_diagnostics.pending_valid && !g_bench.pending) {
                have_request = BenchSteering_Take(&g_bench, now, &request);
                bench_frame = have_request;
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
            if (bench_frame && g_bench.phase != BENCH_STOPPING && g_bench.phase != BENCH_IDLE &&
                (!RemoteService_BenchPermitted() || !RemoteService_State()->enable_request ||
                 (int32_t)(HAL_GetTick() - RemoteService_State()->lease_deadline_ms) >= 0)) {
                BenchSteering_Stop(&g_bench, HAL_GetTick(), 0);
                have_request = 0;
            }
            /* Executor can only address Node 1. Node 2 remains read-only. */
            if (have_request && ((bench_frame && PATROL_CHASSIS_BENCH_STEERING_ENABLED &&
                    (request.id == 0x601U || (request.id == 0 && request.data[1] == 1))) ||
                 (!bench_frame && (request.id == 0x601U || request.id == 0x602U) && request.data[0] == 0x40U)) &&
                HAL_CAN_AddTxMessage(&hcan1, &header, request.data, &mailbox) != HAL_OK)
            {
                taskENTER_CRITICAL();
                if (bench_frame) BenchSteering_TxFailed(&g_bench);
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
                ((snapshot.drive_status_word & 0x6FU) == 0x27U ? 64U : 0U) |
                ((labs(snapshot.left_actual_rpm) > 20 || labs(snapshot.right_actual_rpm) > 20) ? 128U : 0U) |
                (!VehiclePowerOutput_IsOn() ? 256U : 0U);
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
                        g_feedback_gate_mask == 0 && feedback.calibrated &&
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
            feedback.right_actual_rpm = snapshot.right_actual_rpm > 32767 ? 32767 :
                (snapshot.right_actual_rpm < -32768 ? -32768 : (int16_t)snapshot.right_actual_rpm);
            if (feedback.calibrated)
            {
                feedback.steering_actual_mdeg = SteeringAngle_FromControllerCount(
                    snapshot.steering_position_count,
                    PATROL_CHASSIS_BENCH_STEERING_ENABLED && g_coordinate.valid ?
                        g_coordinate.runtime_center : cfg->steering_center_count,
                    cfg->steering_counts_per_degree_milli, cfg->steering_direction);
                feedback.encoder_actual_mdeg = SteeringAngle_FromAbsoluteCount(
                    snapshot.encoder_count, cfg->absolute_center_count,
                    cfg->absolute_counts_per_degree_milli, cfg->absolute_direction);
            }
            feedback.feedback_ms = snapshot.coherent_feedback_ms != 0U ?
                snapshot.coherent_feedback_ms : now - (CHASSIS_FEEDBACK_TIMEOUT_MS + 1U);
            taskENTER_CRITICAL();
            RemoteService_UpdateFeedback(&feedback);
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
        taskENTER_CRITICAL();
        now = HAL_GetTick();
        PatrolDiagnostics_Snapshot(&g_diagnostics, now, &snapshot);
        taskEXIT_CRITICAL();
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
