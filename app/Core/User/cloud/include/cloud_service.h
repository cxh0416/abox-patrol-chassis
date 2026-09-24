#ifndef PATROL_CHASSIS_CLOUD_SERVICE_H
#define PATROL_CHASSIS_CLOUD_SERVICE_H

#include <stdint.h>

#include "main.h"

void CloudService_Init(void);
uint8_t CloudService_NeedsEnrollment(void);
void CloudService_Task(void);
void CloudService_OnUartRxEvent(UART_HandleTypeDef* huart, uint16_t size);
void CloudService_OnUartError(UART_HandleTypeDef* huart);
void CloudTx_NetworkChanged(void);
uint8_t CloudService_ApplyMqttIdentity(const char* vid);

#endif
