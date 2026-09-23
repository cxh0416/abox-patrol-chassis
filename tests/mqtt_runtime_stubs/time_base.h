#ifndef TEST_TIME_BASE_H
#define TEST_TIME_BASE_H
#include <stdint.h>
void TimeBase_UpdateFromCCLK(const char *line);
uint8_t TimeBase_IsValid(void);
#endif
