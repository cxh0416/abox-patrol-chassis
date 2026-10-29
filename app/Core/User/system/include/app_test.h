#ifndef PATROL_CHASSIS_APP_TEST_H
#define PATROL_CHASSIS_APP_TEST_H

#include <stdint.h>

#ifdef PATROL_CHASSIS_APP_FAULT_INJECTION_TEST
// 在专用故障注入构建中执行一次异常或停喂狗测试。等待启动期限后按测试种类触发 HardFault 或返回停喂狗标志，触发状态防止重复注入；正常构建不使用该实现。
uint8_t AppTest_WatchdogStep(void);
#else
// 在未启用 App 故障注入时提供空测试入口。始终返回零，表示不阻止正常看门狗刷新，不产生实际异常。
static inline uint8_t AppTest_WatchdogStep(void) { return 0U; }
#endif

#endif
