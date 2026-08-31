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
#include "remote_service.h"
#include "task.h"

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
static uint8_t g_boot_confirmed;

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
    return (g_health_events != NULL && (xEventGroupGetBits(g_health_events) & VCU_EVENT_CAN_ERROR) != 0U) ? 1U : 0U;
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
    (void)argument;
    vcu_start_can();
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
        while (VcuRtos_CanRxPop(&frame, 0U) == pdPASS)
        {
            /* Node heartbeat, DS402 and feedback decoding is wired in after bench IDs are verified. */
            (void)frame;
        }
        xEventGroupSetBits(g_health_events, VCU_HEALTH_CAN_RX);
    }
}

static void Vcu_CanopenTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    for (;;)
    {
        /* PATROL_CHASSIS_ACTUATION_ENABLED defaults to 0: never send NMT/SDO/PDO here. */
#if PATROL_CHASSIS_ACTUATION_ENABLED
        /* Enabling this build option still requires a bench-approved port implementation. */
#endif
        xEventGroupSetBits(g_health_events, VCU_HEALTH_CANOPEN);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

static void Vcu_EncoderTask(void *argument)
{
    uint8_t request[ABS_ENCODER_REQUEST_SIZE];
    (void)argument;
    AbsoluteEncoder_BuildReadRequest(request);
    for (;;)
    {
        /* UART5 request/response bytes are implemented, but physical polling remains gated until pin ownership is approved. */
        (void)request;
        vTaskDelay(pdMS_TO_TICKS(50U));
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
