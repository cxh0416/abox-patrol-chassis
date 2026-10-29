#ifndef PATROL_FAULT_STOP_H
#define PATROL_FAULT_STOP_H
#include "patrol_canopen.h"

#define PATROL_FAULT_STOP_STEERING 1U
#define PATROL_FAULT_STOP_DRIVE 2U
#define PATROL_FAULT_STOP_LATCH_MS 600U
#define PATROL_FAULT_STOP_TRANSACTION_MS 40U

typedef struct {
    /* pending 时为发送时刻，否则为下一次允许发送的时刻；局部间隔最多 100 ms，
     * 用 16 位差值跨回绕计算。整个故障期限仍使用下面的 32 位 started_ms。 */
    uint16_t at_ms;
    uint8_t step;
    uint8_t pending : 1;
    uint8_t attempts : 2;
} PatrolFaultStopNode;

typedef struct {
    PatrolFaultStopNode nodes[2];
    uint32_t started_ms;
    uint8_t required_mask, disabled_mask, active, latch_due;
} PatrolFaultStop;
_Static_assert(sizeof(PatrolFaultStopNode) == 4U && sizeof(PatrolFaultStop) == 16U,
               "Fault stop RAM budget changed");

// 清空独立故障停机上下文。只重置 RAM；不发送使能、运动目标、复位或驱动器参数。
void PatrolFaultStop_Init(PatrolFaultStop* stop);
// 开始一次故障停机。重复请求不重置期限；两节点分别维护事务，某节点漏应答不会阻塞另一个节点撤使能。
void PatrolFaultStop_Begin(PatrolFaultStop* stop, uint8_t nodes, uint32_t now);
// 推进单事务重试及 600 ms 锁存期限。超期只允许先锁存故障，仍继续安全撤使能重试，直至两节点反馈确认关闭。
void PatrolFaultStop_Tick(PatrolFaultStop* stop, uint32_t now);
// 选取一条故障停机帧：优先给两节点 6040=6，再清行走双轮零速、读取状态。永远不构造使能或非零运动命令。
uint8_t PatrolFaultStop_Take(PatrolFaultStop* stop, uint32_t now, PatrolCanFrame* frame);
// 登记提交失败并稍后重试同一安全步骤，避免在邮箱暂满时忙循环；node 为 1 或 2。
void PatrolFaultStop_TxFailed(PatrolFaultStop* stop, uint8_t node, uint32_t now);
// 校验本流程拥有的 SDO 响应。只有 shutdown 后发出的状态读取确认 Operation Enabled 已清除，才把对应节点标记为撤使能完成。
uint8_t PatrolFaultStop_OnFrame(PatrolFaultStop* stop, const PatrolCanFrame* frame, uint32_t now);
#endif
