#ifndef BENCH_DRIVE_H
#define BENCH_DRIVE_H
#include "bench_steering.h"
/* Drive actuator limit shared by normal control. */
#define BENCH_DRIVE_MAX_RPM 1000
typedef struct {
    BenchPhase phase;
    uint8_t step, pending, read, width, cut_power, confirmations, reversing;
    uint16_t index;
    uint32_t sent_ms, started_ms, zero_ms, left_ms, right_ms, fault;
    int32_t expected;
    int16_t desired, applied, writing;
    int8_t direction;
} BenchDrive;
// 清空行走执行器状态并回到空闲阶段。重置握手、目标、方向和停车计数；仅修改 RAM，不发送 CAN 报文。
void BenchDrive_Init(BenchDrive* b);
// 从空闲状态启动行走驱动器准备流程。非空闲时拒绝；记录起始时间，实际模式写入、零速和 CiA402 使能由后续 Take/OnFrame 逐步推进。
uint8_t BenchDrive_Start(BenchDrive* b, uint32_t now);
// 请求行走执行器有序停车或紧急故障退出。普通停止先取消旧事务，再写左右零速、确认停车并撤使能；emergency 非零则直接锁存故障并置切电请求。
void BenchDrive_Stop(BenchDrive* b, uint32_t now, uint8_t emergency);
// 推进行走目标、事务超时和换向停车状态。反向前明确写双轮零速，只有零速 ACK 之后且比上一对更新的左右反馈才参与三次低速确认；事务、准备、停车超时分别受限。
void BenchDrive_Tick(BenchDrive* b, uint32_t now, uint8_t requested, uint8_t safe,
    int16_t rpm, int32_t left, int32_t right, uint32_t left_ms, uint32_t right_ms);
// 取出行走执行器当前步骤唯一允许发送的报文。Node 2 使用 0x60FF/0x68FF 写双轮目标，右轮符号取反；SDO 在途时不再生成新事务，取出不代表已经送达。
uint8_t BenchDrive_Take(BenchDrive* b, uint32_t now, PatrolCanFrame* f);
// 处理与当前行走 SDO 事务匹配的响应。校验节点、索引、子索引、长度、时限及状态字，Abort 或模式不匹配进入故障；完成双轮零速响应后才建立停车反馈时间基准。
uint8_t BenchDrive_OnFrame(BenchDrive* b, const PatrolCanFrame* f, uint32_t now);
// 将 CAN 请求发送失败转换为行走执行故障。不盲目重发可能已部分执行的运动命令，交由运行层处理停机与供电请求。
void BenchDrive_TxFailed(BenchDrive* b);
#endif
