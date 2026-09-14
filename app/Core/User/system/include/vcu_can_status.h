#ifndef VCU_CAN_STATUS_H
#define VCU_CAN_STATUS_H
#include <stdint.h>
uint32_t VcuRtos_CanRxOverflowCount(void);
uint32_t VcuRtos_CanRawRxCount(uint8_t bus);
uint8_t VcuRtos_CanStarted(void);
uint8_t VcuRtos_CanBusOff(void);
uint8_t VcuRtos_CanBusOffActive(void);
#endif
