#include "patrol_rtos.h"
#include "patrol_runtime.h"
#include "center_calibration.h"
#include "patrol_runtime_port.h"
#include "patrol_can_recovery.h"
#include "patrol_can_remote.h"
#include "patrol_phy_remote.h"

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
#define PATROL_CAN_REPLY_RETRY_MS 2U
#define PATROL_CAN_REPLY_TIMEOUT_MS 500U
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
// 周期执行底盘安全和本地遥控租约监督。独立于网络任务推进撤权，任何目标继续执行仍须通过当前反馈门禁。
static void Patrol_SafetyControlTask(void *argument);
// 消费 CAN 接收队列并按来源分发协议。分离遥控入口、驱动器反馈与可选网关，运动共享状态按临界区约定更新。
static void Patrol_CanRxTask(void *argument);
// 串行推进运动运行层和 CANopen 执行事务。按固定周期调用 Tick，发送前重新检查授权；不以 CAN 提交成功当作真实运动或停车完成。
static void Patrol_CanopenTask(void *argument);
// 执行绝对编码器串口读取、标定写入及复读验证。单一任务拥有 UART 事务，只有有效响应更新反馈时间，结果通过运行层受保护接口发布。
static void Patrol_EncoderTask(void *argument);
// 轮询云业务、共享远控安全与中位标定业务。按已有调度周期串行使用网络和维护上下文，耗时网络步骤不得替代独立安全任务。
static void Patrol_RemoteTask(void *argument);
// 周期输出产品诊断日志。仅在相应诊断配置允许时运行，不在此修改运动控制许可。
static void Patrol_DiagTask(void *argument);
// 周期刷新独立看门狗并执行可选故障注入。故障测试路径按编译开关决定是否故意停止喂狗，正常路径维持主控存活监督。
static void Patrol_WatchdogTask(void *argument);

// 把 HAL CAN 句柄转换为产品总线编号。用于接收统计与来源标记，未知句柄按函数分支返回无效编号。
static uint8_t patrol_can_bus_index(const CAN_HandleTypeDef *hcan)
{
    return (hcan != NULL && hcan->Instance == CAN2) ? 1U : 0U;
}

// 启动 CAN1、CAN2 并开启接收和错误通知。已启动时直接返回，任一步 HAL 操作失败进入 Error_Handler；本函数不配置过滤器或发送运动目标。
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

// 创建底盘任务及静态 CAN 接收队列。设置任务栈、优先级和运行入口，创建结果必须检查；调度启动后各任务按职责更新状态。
void PatrolRtos_Init(void)
{
    PatrolRuntime_Init();
    PatrolCanRemote_Init();
    PatrolPhyRemote_Init();
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

// 从 CAN 中断读取帧并投递到静态队列。使用 ISR 安全 API 与唤醒标志，队列满累计溢出；不在中断中执行运动或 JSON 业务。
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

// 从产品 CAN 接收队列取出一帧。wait_ticks 是 RTOS tick 而非毫秒，返回队列操作结果，由接收任务继续路由处理。
BaseType_t PatrolRtos_CanRxPop(PatrolCanFrame_t *frame, TickType_t wait_ticks)
{
    if (frame == NULL || g_can_rx_queue == NULL) return pdFAIL;
    return xQueueReceive(g_can_rx_queue, frame, wait_ticks);
}

// 读取 CAN 接收队列溢出累计次数。计数用于诊断丢帧风险，不代表驱动器已离线或已停止。
uint32_t PatrolRtos_CanRxOverflowCount(void)
{
    return g_can_rx_overflow_count;
}

// 查询产品 CAN 外设是否已完成启动。启动标志不等于当前无 Bus-Off，也不代表已收到节点反馈。
uint8_t PatrolRtos_CanStarted(void) { return g_can_started; }
// 查询阻止运动的 CAN 总线故障门禁。结合当前实现维护的总线状态使用，恢复门禁仍须经过稳定反馈观察。
uint8_t PatrolRtos_CanBusOff(void)
{
    return (g_can_bus_off_mask & 0x01U) ? 1U : 0U;
}

// 读取指定 CAN 总线的原始接收帧计数。按总线编号区分来源，仅说明收到帧，不证明报文已通过协议和安全校验。
uint32_t PatrolRtos_CanRawRxCount(uint8_t bus)
{
    return bus < 2U ? g_can_raw_rx_count[bus] : 0U;
}

// 查询 CAN 控制器当前是否仍处于活动 Bus-Off。与历史见过 Bus-Off 及恢复门禁分开，用于判断底层是否能继续发送停车报文。
uint8_t PatrolRtos_CanBusOffActive(void)
{
    return (hcan1.Instance->ESR & CAN_ESR_BOFF) != 0U ? 1U : 0U;
}

// 查询本轮是否记录过 Bus-Off 事件。历史标志用于诊断，不因控制器暂时恢复就丢弃故障证据。
uint8_t PatrolRtos_CanBusOffSeen(void)
{
    return (g_can_bus_off_seen_mask & 0x01U) ? 1U : 0U;
}

// 读取最近 Bus-Off 的单调毫秒时间戳。作为恢复样本必须晚于故障事件的基准，不是 UTC 时间。
uint32_t PatrolRtos_CanLastBusOffMs(void) { return g_can_bus_off_at_ms; }
// 读取本轮 CAN 恢复已观察到的有效独立样本数。只有新鲜一致且满足安全条件的反馈才计数，不以轮询次数代替。
uint8_t PatrolRtos_CanRecoverySamples(void) { return g_can_recovery.samples; }

// 用健康的一致反馈推进 Bus-Off 恢复观察。恢复要求故障之后的新样本、三次独立采样及至少一秒稳定区间；不会自动恢复旧运动会话。
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


// 在 CAN FIFO0 回调中投递接收数据并按需唤醒任务。回调处于中断上下文，使用 ISR 接口，业务解析交给接收任务。
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)PatrolRtos_CanRxFromISR(hcan, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

// 记录 CAN 错误和 Bus-Off 事件供安全任务处理。更新故障门禁及恢复基准，不在中断中尝试恢复运动授权。
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

// 周期执行底盘安全和本地遥控租约监督。独立于网络任务推进撤权，任何目标继续执行仍须通过当前反馈门禁。
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
        /* >>>>> PHY-REMOTE: START >>>>> */
        PatrolPhyRemote_Tick(HAL_GetTick(),
            (uint8_t)((hcan2.Instance->ESR & CAN_ESR_BOFF) == 0U));
        /* <<<<< PHY-REMOTE: END <<<<< */
        taskEXIT_CRITICAL();
        xEventGroupSetBits(g_health_events, PATROL_HEALTH_SAFETY);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}

/* >>>>> CAN2-GATEWAY: START >>>>> */
#if PATROL_CHASSIS_CAN2_GATEWAY_ENABLED
// 在网关编译开关允许时转发受约束的 CAN 帧。保留总线来源和既有过滤规则，网关不代替产品会话及运动许可仲裁。
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

// 消费 CAN 接收队列并按来源分发协议。分离遥控入口、驱动器反馈与可选网关，运动共享状态按临界区约定更新。
static void Patrol_CanRxTask(void *argument)
{
    PatrolCanFrame_t frame;
    PatrolCanFrame decoded;
    PatrolCanFrame replies[3];
    uint8_t replies_ready = 0U;
    uint8_t reply_index = 0U;
    TickType_t reply_started = 0U;
    (void)argument;
    patrol_start_can();
    for (;;)
    {
        const TickType_t wait_ticks = replies_ready ?
            pdMS_TO_TICKS(PATROL_CAN_REPLY_RETRY_MS) : pdMS_TO_TICKS(100U);
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
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
            if (frame.bus == 1U) {
                /* >>>>> PHY-REMOTE: START >>>>> */
                /* Handset receiver (0x10A/0x10B) is wired to CAN2; check it
                 * before the CAN2 keyboard command service (0x520). */
                if (!PatrolPhyRemote_OnFrame(&decoded, frame.timestamp_ms, HAL_GetTick())) {
                    PatrolCanRemote_OnFrame(&decoded, frame.timestamp_ms, HAL_GetTick());
                }
                /* <<<<< PHY-REMOTE: END <<<<< */
            } else {
                PatrolRuntime_OnCanFrame(&decoded, frame.timestamp_ms);
            }
            taskEXIT_CRITICAL();
        }
        if (!replies_ready) {
            taskENTER_CRITICAL();
            replies_ready = PatrolCanRemote_TakeReply(replies, HAL_GetTick());
            taskEXIT_CRITICAL();
            reply_index = 0U;
            reply_started = xTaskGetTickCount();
        }
        if (replies_ready) {
            CAN_TxHeaderTypeDef header = {0};
            uint32_t mailbox;
            header.StdId = replies[reply_index].id;
            header.IDE = CAN_ID_STD;
            header.RTR = CAN_RTR_DATA;
            header.DLC = replies[reply_index].dlc;
            if (HAL_CAN_AddTxMessage(&hcan2, &header, replies[reply_index].data,
                                     &mailbox) == HAL_OK) {
                reply_index++;
                if (reply_index >= 3U) replies_ready = 0U;
            } else if ((TickType_t)(xTaskGetTickCount() - reply_started) >=
                       pdMS_TO_TICKS(PATROL_CAN_REPLY_TIMEOUT_MS)) {
                /* A temporary full mailbox must not discard the remaining
                 * correlated frames. Only a persistent transport failure
                 * invalidates the CAN2 remote session. */
                replies_ready = 0U;
                taskENTER_CRITICAL();
                PatrolCanRemote_Tick(HAL_GetTick(), 0U);
                taskEXIT_CRITICAL();
            }
        }
        xEventGroupSetBits(g_health_events, PATROL_HEALTH_CAN_RX);
    }
}

// 串行推进运动运行层和 CANopen 执行事务。按固定周期调用 Tick，发送前重新检查授权；不以 CAN 提交成功当作真实运动或停车完成。
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

// 执行绝对编码器串口读取、标定写入及复读验证。单一任务拥有 UART 事务，只有有效响应更新反馈时间，结果通过运行层受保护接口发布。
static void Patrol_EncoderTask(void *argument)
{
    (void)argument;
    for (;;)
    {
#if PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED
        uint8_t request[ABS_ENCODER_REQUEST_SIZE];
        uint8_t response[ABS_ENCODER_RX_BUFFER_SIZE] = {0};
        uint16_t registers[3];
        uint16_t response_length = 0U;
        uint16_t register_count;
        uint8_t read_config;
        uint8_t response_ok = 0U;
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
        if (status == HAL_OK) {
            /* A half-duplex adapter may deliver the request echo and the
             * encoder reply as separate idle-delimited chunks. Collect at
             * most two chunks, then search the combined buffer for a valid
             * Modbus response. */
            for (uint8_t chunk = 0U; chunk < 2U && response_length < sizeof(response); chunk++) {
                uint16_t received = 0U;
                status = HAL_UARTEx_ReceiveToIdle(
                    &huart5, response + response_length,
                    (uint16_t)(sizeof(response) - response_length), &received, 40U);
                response_length += received;
                if (status == HAL_OK && AbsoluteEncoder_ParseHoldingResponseAnywhere(
                        response, response_length, registers, register_count)) {
                    response_ok = 1U;
                    break;
                }
                if (received == 0U) break;
            }
        }
        taskENTER_CRITICAL();
        if (response_ok)
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
        vTaskDelay(pdMS_TO_TICKS(read_config ? 500U : 20U));
#else
        vTaskDelay(pdMS_TO_TICKS(1000U));
#endif
    }
}

// 轮询云业务、共享远控安全与中位标定业务。按已有调度周期串行使用网络和维护上下文，耗时网络步骤不得替代独立安全任务。
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

// 周期输出产品诊断日志。仅在相应诊断配置允许时运行，不在此修改运动控制许可。
static void Patrol_DiagTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        AppLog_DebugTask();
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

// 周期刷新独立看门狗并执行可选故障注入。故障测试路径按编译开关决定是否故意停止喂狗，正常路径维持主控存活监督。
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

// 处理 FreeRTOS 检测到的任务栈溢出。记录故障邮箱并进入安全终止路径，不能继续使用已可能损坏的任务栈执行业务。
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    PatrolFault_Record(PATROL_FAULT_STACK_OVERFLOW, 0U, 0U);
    NVIC_SystemReset();
    for (;;) {}
}

// 处理 FreeRTOS 内存分配失败。记录不可继续的运行故障并按既有终止路径退出，避免带不完整任务对象运行。
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    PatrolFault_Record(PATROL_FAULT_MALLOC_FAILED, 0U, 0U);
    NVIC_SystemReset();
    for (;;) {}
}

/* Hardware/RTOS boundary for the independently compiled motion modules. */
// 向运动运行层提供 HAL 单调毫秒时钟。供超时和反馈年龄计算使用，不是 UTC 时间。
uint32_t PatrolRuntimePort_Now(void) { return HAL_GetTick(); }
// 进入可嵌套的运动共享状态临界区。应与 Exit 成对使用，禁止在保护区内执行 Flash 擦写或阻塞式外部等待。
void PatrolRuntimePort_Enter(void) { taskENTER_CRITICAL(); }
// 退出一层运动共享状态临界区。必须与当前任务先前 Enter 对应，不能跨任务或从不匹配的 ISR 上下文退出。
void PatrolRuntimePort_Exit(void) { taskEXIT_CRITICAL(); }
// 向运动层转发 CAN 故障门禁状态。复用 RTOS CAN 状态，不由运动模块另行推断控制器恢复。
uint8_t PatrolRuntimePort_CanBusOff(void) { return PatrolRtos_CanBusOff(); }
// 向运动层报告当前活动 Bus-Off。用于区分不能发送的底层故障与历史锁存，不表示旧会话可自动恢复。
uint8_t PatrolRuntimePort_CanBusOffActive(void) { return PatrolRtos_CanBusOffActive(); }
// 向运动层报告 OTA 是否占用受保护流程。忙状态参与运动禁止和 Flash 写入互斥，不允许在升级期间推进普通运动。
uint8_t PatrolRuntimePort_OtaBusy(void) { return OtaService_IsBusy(); }
// 检查当前 CAN 发送路径能否接受请求。只描述控制器与邮箱可用性，不代表目的节点在线或会响应。
uint8_t PatrolRuntimePort_CanTxReady(void) { return HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U; }
// 中止尚在 CAN 邮箱排队的旧发送。用于撤权或停止时减少旧目标滞留，已发送到总线的命令仍须通过后续停车流程处理。
void PatrolRuntimePort_AbortCanTx(void)
{
    (void)HAL_CAN_AbortTxRequest(&hcan1, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
}
// 将一条已仲裁的产品帧提交到 CAN 发送端口。返回 HAL 提交结果；调用方仍须处理发送失败和 SDO 超时，不得据返回成功宣称驱动器已执行。
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
