#ifndef BENCH_STEERING_H
#define BENCH_STEERING_H
#include <stdint.h>
#include "patrol_canopen.h"

typedef enum { BENCH_IDLE, BENCH_PREPARING, BENCH_ACTIVE, BENCH_STOPPING, BENCH_FAULT } BenchPhase;
typedef struct {
    BenchPhase phase;
    uint8_t step, pending, read, width, cut_power, limits_aligned, upper_first, preserve_limits;
    uint16_t index;
    uint32_t sent_ms, started_ms, stop_ms, fault;
    int32_t minimum, maximum, center, hold, desired, applied, expected;
    int32_t calibration_center, cached_minimum, cached_maximum, previous_minimum;
    uint16_t limit_mdeg;
    uint16_t profile_rpm, applied_profile_rpm, speed_write_rpm;
    uint8_t tpdo_check, subindex, stop_confirmed, target_pending;
} BenchSteering;

// 清空转向执行器全部运行状态。仅操作状态结构，不写驱动器；需要保留的已对齐限位缓存由启动函数显式恢复。
void BenchSteering_Init(BenchSteering* b);
// 启动正常转向位置模式握手。要求空闲且软限位档位合法；保存实测保持位置、运行中位及标定中位，后续逐项读写验证驱动器对象。
uint8_t BenchSteering_Start(BenchSteering* b, int32_t actual, int32_t center,
    int32_t calibration_center, uint16_t limit, uint32_t now);
// 启动中位标定专用转向握手。仅允许合法范围和空闲执行器，置 preserve_limits 保留现有驱动限位；不能替代普通运动的标定与坐标门禁。
uint8_t BenchSteering_StartCommissioning(BenchSteering* b, int32_t actual, int32_t center,
    uint32_t limit, uint32_t now);
// 请求转向撤使能或故障切电。普通停车进入有期限的撤使能确认；准备过程中限位尚未成对对齐则判故障，避免继续使用半更新的限位。
void BenchSteering_Stop(BenchSteering* b, uint32_t now, uint8_t cut_power);
// 根据请求、安全条件和时间推进转向执行器。检查目标范围、计数转换、准备与事务期限；活动阶段按误差切换轮廓速度，停止阶段保留专用确认期限。
void BenchSteering_Tick(BenchSteering* b, uint32_t now, uint8_t requested, uint8_t safe,
                        int32_t angle, int32_t actual_angle);
// 构造当前转向步骤的 NMT 或 SDO 请求。只控制 Node 1，依次处理限位、模式、速度、保持位置、使能、定位触发及停止；只允许一个在途事务。
uint8_t BenchSteering_Take(BenchSteering* b, uint32_t now, PatrolCanFrame* frame);
// 校验并消费当前转向事务的驱动器响应。确认索引、长度、模式、状态字和写回值；按状态推进限位对齐及定位握手，不能把 ACK 当成真实转角到位。
uint8_t BenchSteering_OnFrame(BenchSteering* b, const PatrolCanFrame* frame, uint32_t now);
// 记录转向 CAN 请求发送失败。交由统一故障入口锁存，不能以一次报文提交失败推断驱动器处于安全状态。
void BenchSteering_TxFailed(BenchSteering* b);
// 查询转向停止过程是否已取得撤使能确认。用于运行层区分等待驱动器确认与最后完成步骤，避免提前释放执行器归属。
uint8_t BenchSteering_StopFinishReady(const BenchSteering* b);
#endif
