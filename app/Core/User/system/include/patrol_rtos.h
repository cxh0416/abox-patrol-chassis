#ifndef PATROL_RTOS_H
#define PATROL_RTOS_H

#include <stdint.h>
#include "patrol_can_status.h"

#include "can.h"
#include "FreeRTOS.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PATROL_CAN_RX_QUEUE_LENGTH 32U

typedef struct
{
    uint8_t bus;
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    uint32_t timestamp_ms;
} PatrolCanFrame_t;

// 创建底盘任务及静态 CAN 接收队列。设置任务栈、优先级和运行入口，创建结果必须检查；调度启动后各任务按职责更新状态。
void PatrolRtos_Init(void);
// 从 CAN 中断读取帧并投递到静态队列。使用 ISR 安全 API 与唤醒标志，队列满累计溢出；不在中断中执行运动或 JSON 业务。
BaseType_t PatrolRtos_CanRxFromISR(CAN_HandleTypeDef *hcan, BaseType_t *higher_priority_task_woken);
// 从产品 CAN 接收队列取出一帧。wait_ticks 是 RTOS tick 而非毫秒，返回队列操作结果，由接收任务继续路由处理。
BaseType_t PatrolRtos_CanRxPop(PatrolCanFrame_t *frame, TickType_t wait_ticks);

#ifdef __cplusplus
}
#endif

#endif
