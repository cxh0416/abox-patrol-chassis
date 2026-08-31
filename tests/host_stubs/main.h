#ifndef PATROL_CHASSIS_HOST_TEST_MAIN_H
#define PATROL_CHASSIS_HOST_TEST_MAIN_H

#include <stdint.h>

typedef struct { int unused; } UART_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef* uart, uint8_t* data, uint16_t len, uint32_t timeout);

#endif
