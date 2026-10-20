#include "patrol_rtos.h"
#include "patrol_runtime.h"
#include "center_calibration.h"
#include "patrol_runtime_port.h"
#include "patrol_can_recovery.h"
#include "patrol_can_remote.h"

#include <string.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "boot_state.h"
#include "app_log.h"
#include "app_test.h"
#include "absolute_encoder.h"
#include "cloud_service.h"
#include "fault_mailbox.h"
#include "iwdg.h"
#include "ota_service.h"
#include "patrol_canopen.h"
#include "patrol_diagnostics.h"
#include "remote_service.h"
#include "task.h"
#include "usart.h"
#include "vehicle_steering_profile.h"
#include "vehicle_power_output.h"
#include "vehicle_power_port.h"

#ifndef PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
#define PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED 0
#endif
/* >>>>> CAN2-GATEWAY: START >>>>> */
#ifndef PATROL_CHASSIS_CAN2_GATEWAY_ENABLED
#define PATROL_CHASSIS_CAN2_GATEWAY_ENABLED 0
#endif
/* <<<<< CAN2-GATEWAY: END <<<<< */

#define PATROL_HEALTH_SAFETY  (1U << 0)
#define PATROL_HEALTH_CAN_RX  (1U << 1)
#define PATROL_HEALTH_CANOPEN  (1U << 2)
#define PATROL_EVENT_CAN_ERROR (1U << 3)
#define PATROL_HEALTH_REQUIRED (PATROL_HEALTH_SAFETY | PATROL_HEALTH_CAN_RX | PATROL_HEALTH_CANOPEN)

#define PATROL_SAFETY_STACK_WORDS 256U
#define PATROL_CAN_RX_STACK_WORDS 256U
#define PATROL_CANOPEN_STACK_WORDS 256U
#define PATROL_ENCODER_STACK_WORDS 192U
#define PATROL_REMOTE_STACK_WORDS 1024U
#define PATROL_DIAG_STACK_WORDS 256U
/*
 * BootState_Confirm() persists a Boot V2 record from this task.  The ARM
 * release stack reports show a 992-byte static call-chain peak before the
 * HAL flash helper margin, so 256 words is not sufficient for trial confirm.
 */
#define PATROL_WATCHDOG_STACK_WORDS 384U

static StaticQueue_t g_can_rx_queue_control;
static PatrolCanFrame_t g_can_rx_queue_storage[PATROL_CAN_RX_QUEUE_LENGTH];
static QueueHandle_t g_can_rx_queue;
static StaticEventGroup_t g_health_control;
static EventGroupHandle_t g_health_events;

static StaticTask_t g_safety_tcb;
static StaticTask_t g_can_rx_tcb;
static StaticTask_t g_canopen_tcb;
static StaticTask_t g_encoder_tcb;
static StaticTask_t g_remote_tcb;
static StaticTask_t g_diag_tcb;
static StaticTask_t g_watchdog_tcb;
static StackType_t g_safety_stack[PATROL_SAFETY_STACK_WORDS];
static StackType_t g_can_rx_stack[PATROL_CAN_RX_STACK_WORDS];
static StackType_t g_canopen_stack[PATROL_CANOPEN_STACK_WORDS];
static StackType_t g_encoder_stack[PATROL_ENCODER_STACK_WORDS];
static StackType_t g_remote_stack[PATROL_REMOTE_STACK_WORDS];
static StackType_t g_diag_stack[PATROL_DIAG_STACK_WORDS];
static StackType_t g_watchdog_stack[PATROL_WATCHDOG_STACK_WORDS];
static TaskHandle_t g_safety_task;
static TaskHandle_t g_can_rx_task;
static TaskHandle_t g_canopen_task;
static TaskHandle_t g_encoder_task;
static TaskHandle_t g_remote_task;
static TaskHandle_t g_diag_task;
static TaskHandle_t g_watchdog_task;
static volatile uint32_t g_can_rx_overflow_count;
static volatile uint32_t g_can_raw_rx_count[2];
static volatile uint8_t g_can_started;
static volatile uint8_t g_can_bus_off_mask;
static volatile uint8_t g_can_bus_off_seen_mask;
static volatile uint32_t g_can_bus_off_at_ms;
static PatrolCanRecovery g_can_recovery;
static uint8_t g_boot_confirmed;
static void Patrol_SafetyControlTask(void *argument);
static void Patrol_CanRxTask(void *argument);
static void Patrol_CanopenTask(void *argument);
static void Patrol_EncoderTask(void *argument);
static void Patrol_RemoteTask(void *argument);
static void Patrol_DiagTask(void *argument);
static void Patrol_WatchdogTask(void *argument);

static uint8_t patrol_can_bus_index(const CAN_HandleTypeDef *hcan)
{
    return (hcan != NULL && hcan->Instance == CAN2) ? 1U : 0U;
}

static void patrol_start_can(void)
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

void PatrolRtos_Init(void)
{
    PatrolRuntime_Init();
    PatrolCanRemote_Init();
    g_can_rx_queue = xQueueCreateStatic(PATROL_CAN_RX_QUEUE_LENGTH,
                                         sizeof(PatrolCanFrame_t),
                                         (uint8_t *)g_can_rx_queue_storage,
                                         &g_can_rx_queue_control);
    g_health_events = xEventGroupCreateStatic(&g_health_control);

    g_safety_task = xTaskCreateStatic(Patrol_SafetyControlTask, "SafetyControl",
                                      PATROL_SAFETY_STACK_WORDS, NULL, 6U,
                                      g_safety_stack, &g_safety_tcb);
    g_can_rx_task = xTaskCreateStatic(Patrol_CanRxTask, "CanRx",
                                      PATROL_CAN_RX_STACK_WORDS, NULL, 5U,
                                      g_can_rx_stack, &g_can_rx_tcb);
    g_canopen_task = xTaskCreateStatic(Patrol_CanopenTask, "Canopen",
                                      PATROL_CANOPEN_STACK_WORDS, NULL, 4U,
                                      g_canopen_stack, &g_canopen_tcb);
    g_encoder_task = xTaskCreateStatic(Patrol_EncoderTask, "EncoderUart5",
                                       PATROL_ENCODER_STACK_WORDS, NULL, 4U,
                                       g_encoder_stack, &g_encoder_tcb);
    g_watchdog_task = xTaskCreateStatic(Patrol_WatchdogTask, "Watchdog",
                                        PATROL_WATCHDOG_STACK_WORDS, NULL, 3U,
                                        g_watchdog_stack, &g_watchdog_tcb);
    g_remote_task = xTaskCreateStatic(Patrol_RemoteTask, "Remote",
                                      PATROL_REMOTE_STACK_WORDS, NULL, 2U,
                                      g_remote_stack, &g_remote_tcb);
    g_diag_task = xTaskCreateStatic(Patrol_DiagTask, "Diag",
                                    PATROL_DIAG_STACK_WORDS, NULL, 1U,
                                    g_diag_stack, &g_diag_tcb);

    configASSERT(g_can_rx_queue != NULL);
    configASSERT(g_health_events != NULL);
    configASSERT(g_safety_task != NULL && g_can_rx_task != NULL && g_canopen_task != NULL);
    configASSERT(g_encoder_task != NULL);
    configASSERT(g_watchdog_task != NULL && g_remote_task != NULL && g_diag_task != NULL);

#if defined(DEBUG)
    __HAL_DBGMCU_FREEZE_IWDG();
#endif
}

BaseType_t PatrolRtos_CanRxFromISR(CAN_HandleTypeDef *hcan, BaseType_t *higher_priority_task_woken)
{
    CAN_RxHeaderTypeDef header;
    PatrolCanFrame_t frame;

    if (hcan == NULL || g_can_rx_queue == NULL) return pdFAIL;
    memset(&frame, 0, sizeof(frame));
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, frame.data) != HAL_OK) return pdFAIL;
    frame.bus = patrol_can_bus_index(hcan);
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

BaseType_t PatrolRtos_CanRxPop(PatrolCanFrame_t *frame, TickType_t wait_ticks)
{
    if (frame == NULL || g_can_rx_queue == NULL) return pdFAIL;
    return xQueueReceive(g_can_rx_queue, frame, wait_ticks);
}

uint32_t PatrolRtos_CanRxOverflowCount(void)
{
    return g_can_rx_overflow_count;
}

uint8_t PatrolRtos_CanStarted(void) { return g_can_started; }
uint8_t PatrolRtos_CanBusOff(void)
{
    return (g_can_bus_off_mask & 0x01U) ? 1U : 0U;
}

uint32_t PatrolRtos_CanRawRxCount(uint8_t bus)
{
    return bus < 2U ? g_can_raw_rx_count[bus] : 0U;
}

uint8_t PatrolRtos_CanBusOffActive(void)
{
    return (hcan1.Instance->ESR & CAN_ESR_BOFF) != 0U ? 1U : 0U;
}

uint8_t PatrolRtos_CanBusOffSeen(void)
{
    return (g_can_bus_off_seen_mask & 0x01U) ? 1U : 0U;
}

uint32_t PatrolRtos_CanLastBusOffMs(void) { return g_can_bus_off_at_ms; }
uint8_t PatrolRtos_CanRecoverySamples(void) { return g_can_recovery.samples; }

void PatrolRtos_ObserveCanRecovery(uint32_t sample_ms, uint8_t healthy)
{
    uint32_t now = HAL_GetTick();
    taskENTER_CRITICAL();
    if ((g_can_bus_off_mask & 0x01U) != 0U)
    {
        if (!VehiclePowerOutput_IsOn() || PatrolRtos_CanBusOffActive())
            PatrolCanRecovery_Reset(&g_can_recovery);
        else if (PatrolCanRecovery_Observe(&g_can_recovery, now, sample_ms,
                g_can_bus_off_at_ms, healthy))
            g_can_bus_off_mask &= (uint8_t)~0x01U;
    }
    taskEXIT_CRITICAL();
}


void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)PatrolRtos_CanRxFromISR(hcan, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    /* HAL ErrorCode accumulates earlier errors. Read the hardware flag so a
     * later unrelated error cannot re-latch an already recovered Bus-Off. */
    if (g_health_events != NULL && hcan != NULL && (hcan->Instance->ESR & CAN_ESR_BOFF) != 0U)
    {
        uint8_t bit = hcan->Instance == CAN1 ? 0x01U : 0x02U;
        g_can_bus_off_mask |= bit;
        g_can_bus_off_seen_mask |= bit;
        if (hcan->Instance == CAN1)
        {
            g_can_bus_off_at_ms = HAL_GetTick();
            PatrolCanRecovery_Reset(&g_can_recovery);
            xEventGroupSetBitsFromISR(g_health_events, PATROL_EVENT_CAN_ERROR, &higher_priority_task_woken);
        }
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void Patrol_SafetyControlTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;)
    {
        VehiclePowerPort_Poll(HAL_GetTick());
        taskENTER_CRITICAL();
        RemoteService_SafetyTick(HAL_GetTick());
        PatrolCanRemote_Tick(HAL_GetTick(),
            (uint8_t)((hcan2.Instance->ESR & CAN_ESR_BOFF) == 0U));
        taskEXIT_CRITICAL();
        xEventGroupSetBits(g_health_events, PATROL_HEALTH_SAFETY);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

/* >>>>> CAN2-GATEWAY: START >>>>> */
#if PATROL_CHASSIS_CAN2_GATEWAY_ENABLED
static void patrol_can_gateway_forward(const PatrolCanFrame_t *frame)
{
    /* Offline remote: transparently bridge CAN2 (remote port) and CAN1
     * (chassis CANopen). Standard data frames are relayed verbatim so a PC
     * CANopen master on CAN2 can command the Node 1 steering and Node 2 dual
     * drive on CAN1. Extended/remote frames are not forwarded. */
    CAN_TxHeaderTypeDef header = {0};
    CAN_HandleTypeDef *target;
    uint32_t mailbox;
    if (frame == 0 || frame->header.IDE != CAN_ID_STD || frame->header.RTR != CAN_RTR_DATA)
        return;
    target = (frame->bus == 1U) ? &hcan1 : &hcan2;
    header.StdId = frame->header.StdId;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = frame->header.DLC;
    header.TransmitGlobalTime = DISABLE;
    (void)HAL_CAN_AddTxMessage(target, &header, (uint8_t *)frame->data, &mailbox);
}
#endif
/* <<<<< CAN2-GATEWAY: END <<<<< */

static void Patrol_CanRxTask(void *argument)
{
    PatrolCanFrame_t frame;
    PatrolCanFrame decoded;
    PatrolCanFrame replies[3];
    (void)argument;
    patrol_start_can();
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
        while (PatrolRtos_CanRxPop(&frame, 0U) == pdPASS)
        {
/* >>>>> CAN2-GATEWAY: START >>>>> */
#if PATROL_CHASSIS_CAN2_GATEWAY_ENABLED
            patrol_can_gateway_forward(&frame);
#endif
/* <<<<< CAN2-GATEWAY: END <<<<< */
            if (frame.header.IDE != CAN_ID_STD || frame.header.RTR != CAN_RTR_DATA ||
                frame.header.DLC > 8U)
                continue;
            memset(&decoded, 0, sizeof(decoded));
            decoded.id = (uint16_t)frame.header.StdId;
            decoded.dlc = (uint8_t)frame.header.DLC;
            memcpy(decoded.data, frame.data, sizeof(decoded.data));
            taskENTER_CRITICAL();
            if (frame.bus == 1U)
                PatrolCanRemote_OnFrame(&decoded, frame.timestamp_ms, HAL_GetTick());
            else
                PatrolRuntime_OnCanFrame(&decoded, frame.timestamp_ms);
            taskEXIT_CRITICAL();
        }
        taskENTER_CRITICAL();
        uint8_t reply = PatrolCanRemote_TakeReply(replies, HAL_GetTick());
        taskEXIT_CRITICAL();
        if (reply) {
            for (unsigned i=0;i<3;i++) {
                CAN_TxHeaderTypeDef header={0};
                uint32_t mailbox;
                header.StdId=replies[i].id; header.IDE=CAN_ID_STD;
                header.RTR=CAN_RTR_DATA; header.DLC=replies[i].dlc;
                if (HAL_CAN_AddTxMessage(&hcan2,&header,replies[i].data,&mailbox)!=HAL_OK) {
                    taskENTER_CRITICAL();
                    PatrolCanRemote_Tick(HAL_GetTick(),0U);
                    taskEXIT_CRITICAL();
                    break;
                }
            }
        }
        xEventGroupSetBits(g_health_events, PATROL_HEALTH_CAN_RX);
    }
}

static void Patrol_CanopenTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;) {
#if PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
        PatrolRuntime_Tick();
#endif
        xEventGroupSetBits(g_health_events, PATROL_HEALTH_CANOPEN);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

static void Patrol_EncoderTask(void *argument)
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
        write_center = PatrolRuntime_BeginEncoderCycle(&snapshot);
        taskEXIT_CRITICAL();
        if (write_center) {
            AbsoluteEncoder_BuildWriteSingleRequest(3U, VEHICLE_ABSOLUTE_ZERO_COMMAND, request);
            (void)HAL_UART_AbortReceive(&huart5);
            __HAL_UART_FLUSH_DRREGISTER(&huart5);
            status = HAL_UART_Transmit(&huart5, request, sizeof(request), 30U);
            if (status == HAL_OK) {
                HAL_StatusTypeDef receive_status = HAL_UARTEx_ReceiveToIdle(
                    &huart5, response, sizeof(response), &response_length, 40U);
                taskENTER_CRITICAL();
                PatrolRuntime_EncoderWriteCompleted((receive_status == HAL_OK &&
                    AbsoluteEncoder_ParseWriteSingleResponse(response, response_length, 3U,
                        VEHICLE_ABSOLUTE_ZERO_COMMAND)) ? 2U : 5U);
                taskEXIT_CRITICAL();
            } else {
                taskENTER_CRITICAL();
                PatrolRuntime_EncoderWriteCompleted(3U);
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
                PatrolRuntime_OnEncoderConfig(registers, now);
            else
                PatrolRuntime_OnEncoderCount(
                    (uint32_t)registers[0] | (uint32_t)registers[1] << 16, now);
        }
        else if (response_length >= 2U && response[0] == ABS_ENCODER_ADDRESS &&
                 response[1] == (uint8_t)(0x03U | 0x80U))
            PatrolRuntime_OnEncoderError(PATROL_ENCODER_ERROR_EXCEPTION);
        else if (status == HAL_TIMEOUT || response_length == 0U)
            PatrolRuntime_OnEncoderError(PATROL_ENCODER_ERROR_TIMEOUT);
        else if (response_length >= 5U && AbsoluteEncoder_Crc16(response,
                 (uint16_t)(response_length - 2U)) != ((uint16_t)response[response_length - 2U] |
                 (uint16_t)response[response_length - 1U] << 8))
            PatrolRuntime_OnEncoderError(PATROL_ENCODER_ERROR_CRC);
        else if (status != HAL_OK)
            PatrolRuntime_OnEncoderError(PATROL_ENCODER_ERROR_TRANSPORT);
        else
            PatrolRuntime_OnEncoderError(PATROL_ENCODER_ERROR_FORMAT);
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(read_config ? 500U : 100U));
#else
        vTaskDelay(pdMS_TO_TICKS(1000U));
#endif
    }
}

static void Patrol_RemoteTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        CloudService_Task();
        RemoteService_Task();
        CenterCalibration_Tick(HAL_GetTick());
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void Patrol_DiagTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        AppLog_DebugTask();
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

static void Patrol_WatchdogTask(void *argument)
{
    const EventBits_t required = PATROL_HEALTH_REQUIRED;
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
                (xEventGroupGetBits(g_health_events) & PATROL_EVENT_CAN_ERROR) == 0U)
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

/* Hardware/RTOS boundary for the independently compiled motion modules. */
uint32_t PatrolRuntimePort_Now(void) { return HAL_GetTick(); }
void PatrolRuntimePort_Enter(void) { taskENTER_CRITICAL(); }
void PatrolRuntimePort_Exit(void) { taskEXIT_CRITICAL(); }
uint8_t PatrolRuntimePort_CanBusOff(void) { return PatrolRtos_CanBusOff(); }
uint8_t PatrolRuntimePort_CanBusOffActive(void) { return PatrolRtos_CanBusOffActive(); }
uint8_t PatrolRuntimePort_OtaBusy(void) { return OtaService_IsBusy(); }
uint8_t PatrolRuntimePort_CanTxReady(void) { return HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U; }
void PatrolRuntimePort_AbortCanTx(void)
{
    (void)HAL_CAN_AbortTxRequest(&hcan1, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
}
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* frame)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;
    /* The final CAN port rejects every motion frame in a non-actuating image. */
    if (!PATROL_CHASSIS_ACTUATION_ENABLED &&
        !(frame->id >= 0x601U && frame->id <= 0x602U && frame->data[0] == 0x40U))
        return 0U;
    header.StdId = frame->id;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = frame->dlc;
    header.TransmitGlobalTime = DISABLE;
    return HAL_CAN_AddTxMessage(&hcan1, &header, (uint8_t*)frame->data, &mailbox) == HAL_OK;
}
