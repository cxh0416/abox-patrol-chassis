#ifndef PATROL_ENROLLMENT_H
#define PATROL_ENROLLMENT_H

#include "abox_ec800_at.h"

// 绑定公共注册服务与产品回调及固定缓冲区。注册 URL、响应和请求 ID 使用明确容量的共享存储，复用平台服务而不复制注册状态机。
uint8_t PatrolEnrollment_Init(ABoxEc800At *at);
// 轮询当前身份注册状态机。由云任务统一调用，避免与其他 AT 使用者并行推进同一服务。
void PatrolEnrollment_Task(void);
// 通知注册服务模块已复位。使旧模块会话和在途假设失效，后续由公共服务重建所需状态。
void PatrolEnrollment_AfterModemReset(void);

#endif
