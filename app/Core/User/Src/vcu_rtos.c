#include "vcu_rtos.h"

#include <string.h>

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
#include "task.h"
#include "usart.h"
#include "vcu_config.h"

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
#define VCU_WATCHDOG_STACK_WORDS 256U

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
static volatile uint8_t g_can_started;
static volatile uint8_t g_can_bus_off_mask;
static uint8_t g_boot_confirmed;
static PatrolDiagnostics g_diagnostics;

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

static void Vcu_SafetyControlTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;)
    {
        RemoteService_SafetyTick(HAL_GetTick());
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
            PatrolDiagnostics_OnCanFrame(&g_diagnostics, &decoded, frame.timestamp_ms);
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
        uint32_t now = HAL_GetTick();
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U)
        {
            taskENTER_CRITICAL();
            have_request = PatrolDiagnostics_TakeSdoRead(&g_diagnostics, now, &request);
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
            /* Read-only builds emit only SDO upload requests to the two approved nodes. */
            if ((request.id == 0x601U || request.id == 0x602U) && request.data[0] == 0x40U &&
                HAL_CAN_AddTxMessage(&hcan1, &header, request.data, &mailbox) != HAL_OK)
            {
                taskENTER_CRITICAL();
                PatrolDiagnostics_OnSdoTxError(&g_diagnostics, now);
                taskEXIT_CRITICAL();
            }
        }
        if ((int32_t)(now - feedback_due) >= 0)
        {
            PatrolDiagnosticsSnapshot snapshot;
            ChassisFeedback feedback;
            const VcuRemoteConfig_t* cfg = VcuConfig_Get();
            int64_t angle;
            memset(&feedback, 0, sizeof(feedback));
            taskENTER_CRITICAL();
            PatrolDiagnostics_Snapshot(&g_diagnostics, now, &snapshot);
            taskEXIT_CRITICAL();
            feedback.calibrated = cfg != NULL ? cfg->calibrated : 0U;
            feedback.steering_online = snapshot.steering_online;
            feedback.drive_online = snapshot.drive_online;
            feedback.can_bus_off = VcuRtos_CanBusOff();
            feedback.drive_fault = snapshot.drive_fault;
            feedback.ota_busy = OtaService_IsBusy();
            feedback.power_ready = 0U;
            feedback.left_actual_rpm = snapshot.left_actual_rpm > 32767 ? 32767 :
                (snapshot.left_actual_rpm < -32768 ? -32768 : (int16_t)snapshot.left_actual_rpm);
            feedback.right_actual_rpm = snapshot.right_actual_rpm > 32767 ? 32767 :
                (snapshot.right_actual_rpm < -32768 ? -32768 : (int16_t)snapshot.right_actual_rpm);
            if (feedback.calibrated)
            {
                angle = (int64_t)(snapshot.steering_position_count - cfg->steering_center_count) *
                        9LL * cfg->steering_direction / 5LL;
                feedback.steering_actual_mdeg = angle > INT32_MAX ? INT32_MAX :
                    (angle < INT32_MIN ? INT32_MIN : (int32_t)angle);
                angle = ((int64_t)snapshot.encoder_count - cfg->absolute_center_count) * 1000000LL *
                        cfg->absolute_direction / cfg->absolute_counts_per_degree_milli;
                feedback.encoder_actual_mdeg = angle > INT32_MAX ? INT32_MAX :
                    (angle < INT32_MIN ? INT32_MIN : (int32_t)angle);
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
        uint32_t now = HAL_GetTick();
        taskENTER_CRITICAL();
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
