#ifndef PATROL_RUNTIME_PORT_H
#define PATROL_RUNTIME_PORT_H
#include <stdint.h>
#include "patrol_canopen.h"

/* Implemented by the RTOS adapter or deterministic host stubs. Locks nest. */
// 向运动运行层提供 HAL 单调毫秒时钟。供超时和反馈年龄计算使用，不是 UTC 时间。
uint32_t PatrolRuntimePort_Now(void);
// 进入可嵌套的运动共享状态临界区。应与 Exit 成对使用，禁止在保护区内执行 Flash 擦写或阻塞式外部等待。
void PatrolRuntimePort_Enter(void);
// 退出一层运动共享状态临界区。必须与当前任务先前 Enter 对应，不能跨任务或从不匹配的 ISR 上下文退出。
void PatrolRuntimePort_Exit(void);
// 向运动层转发 CAN 故障门禁状态。复用 RTOS CAN 状态，不由运动模块另行推断控制器恢复。
uint8_t PatrolRuntimePort_CanBusOff(void);
// 向运动层报告当前活动 Bus-Off。用于区分不能发送的底层故障与历史锁存，不表示旧会话可自动恢复。
uint8_t PatrolRuntimePort_CanBusOffActive(void);
// 向运动层报告 OTA 是否占用受保护流程。忙状态参与运动禁止和 Flash 写入互斥，不允许在升级期间推进普通运动。
uint8_t PatrolRuntimePort_OtaBusy(void);
// 检查当前 CAN 发送路径能否接受请求。只描述控制器与邮箱可用性，不代表目的节点在线或会响应。
uint8_t PatrolRuntimePort_CanTxReady(void);
// 中止尚在 CAN 邮箱排队的旧发送。用于撤权或停止时减少旧目标滞留，已发送到总线的命令仍须通过后续停车流程处理。
void PatrolRuntimePort_AbortCanTx(void);
// 将一条已仲裁的产品帧提交到 CAN 发送端口。返回 HAL 提交结果；调用方仍须处理发送失败和 SDO 超时，不得据返回成功宣称驱动器已执行。
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* frame);
#endif
