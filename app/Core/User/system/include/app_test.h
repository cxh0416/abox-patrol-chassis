#ifndef PATROL_CHASSIS_APP_TEST_H
#define PATROL_CHASSIS_APP_TEST_H

#include <stdint.h>

#ifdef PATROL_CHASSIS_APP_FAULT_INJECTION_TEST
uint8_t AppTest_WatchdogStep(void);
#else
static inline uint8_t AppTest_WatchdogStep(void) { return 0U; }
#endif

#endif
