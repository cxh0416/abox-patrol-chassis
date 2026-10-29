#ifndef PATROL_CHASSIS_STEERING_ANGLE_H
#define PATROL_CHASSIS_STEERING_ANGLE_H

#include <stdint.h>

typedef struct {
    uint8_t valid, tracking, samples;
    uint32_t quiet_since, last_sample_ms, anchor_absolute;
    int32_t anchor_driver, runtime_center;
} SteeringCoordinate;
// 使当前上电周期的转向运行坐标失效。清除中位建立与稳定样本，不修改 Flash 中的机械标定参数。
void SteeringCoordinate_Reset(SteeringCoordinate* coordinate);
// 用一致的驱动器和编码器反馈建立本次运行中位。仅在 ready 条件与独立采样要求满足时推进；运行坐标用于处理驱动器位置计数上电变化，不等同于重新标定。
void SteeringCoordinate_Observe(SteeringCoordinate* coordinate, uint8_t ready,
    int32_t driver, uint32_t absolute, uint32_t absolute_center, uint32_t sample_ms, uint32_t now);
// 判断已有运行坐标是否因反馈或标定变化失效。根据门禁、节点在线和标定一致性判断，失效后应走安全退出而非带使能重新置零。
uint8_t SteeringCoordinate_EpochLost(uint16_t feedback_gate_mask, uint8_t steering_online,
    uint8_t calibration_unchanged);

// 将毫度目标转换为带方向的驱动器位置计数。使用 64 位中间量并检查比例、方向及计数限位；仅在转换成功时向 target 提交结果。
uint8_t SteeringAngle_ToControllerCount(int32_t angle_mdeg, int32_t center,
    uint32_t counts_milli, int8_t direction, int32_t minimum, int32_t maximum, int32_t* target);

// 将驱动器位置计数相对中位换算为毫度。内部使用宽位差值，方向和比例来自本车标定，不与编码器计数直接混用。
int32_t SteeringAngle_FromControllerCount(int32_t count, int32_t center,
                                          uint32_t counts_per_degree_milli,
                                          int8_t direction);
// 将绝对编码器计数相对中位换算为毫度。先按编码器计数特性求差，再统一使用标定比例与方向。
int32_t SteeringAngle_FromAbsoluteCount(uint32_t count, uint32_t center,
                                        uint32_t counts_per_degree_milli,
                                        int8_t direction);
// 验证绝对编码器参数并检查角度范围。参数合法时先写回换算角度，即使超限也会输出该角度并返回零；参数非法则不写输出。
uint8_t SteeringAngle_AbsoluteWithinLimit(uint32_t count, uint32_t center,
    uint32_t counts_per_degree_milli, int8_t direction, int32_t limit_mdeg,
    int32_t* angle_mdeg);
// 将连续单圈绝对计数展开为累计相对计数。根据单圈模数处理过零，拒绝超过 max_step_counts 的不可信跳变，避免误判为正常转向位移。
uint8_t SteeringAngle_AccumulateSingleTurn(uint32_t previous, uint32_t current,
    uint32_t single_turn_counts, uint32_t max_step_counts, int32_t* accumulated);

#endif
