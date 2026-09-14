#ifndef VCU_RTOS_H
#define VCU_RTOS_H

#include <stdint.h>
#include "vcu_can_status.h"

#include "can.h"
#include "FreeRTOS.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VCU_CAN_RX_QUEUE_LENGTH 32U

typedef struct
{
    uint8_t bus;
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    uint32_t timestamp_ms;
} VcuCanFrame_t;

void VcuRtos_Init(void);
BaseType_t VcuRtos_CanRxFromISR(CAN_HandleTypeDef *hcan, BaseType_t *higher_priority_task_woken);
BaseType_t VcuRtos_CanRxPop(VcuCanFrame_t *frame, TickType_t wait_ticks);

#ifdef __cplusplus
}
#endif

#endif
