#ifndef PATROL_CAN_STATUS_H
#define PATROL_CAN_STATUS_H
#include <stdint.h>
uint32_t PatrolRtos_CanRxOverflowCount(void);
uint32_t PatrolRtos_CanRawRxCount(uint8_t bus);
uint8_t PatrolRtos_CanStarted(void);
uint8_t PatrolRtos_CanBusOff(void);
uint8_t PatrolRtos_CanBusOffActive(void);
#endif
