#ifndef PATROL_AUTO_CENTER_H
#define PATROL_AUTO_CENTER_H

#include "chassis_control.h"

#define PATROL_AUTO_CENTER_TIMEOUT_MS 30000U
#define PATROL_AUTO_CENTER_TOLERANCE_MDEG 1000

typedef struct {
    uint32_t started_ms;
    uint32_t last_sample_ms;
    int32_t hold_mdeg;
    uint8_t requested;
    uint8_t moving;
    uint8_t centered_samples;
    uint8_t failed;
} PatrolAutoCenter;

// 登记一次本地自动回中请求并保存保持角度。记录起始时间及零速确认上下文，先等待行走停稳再允许转向回中。
void PatrolAutoCenter_Request(PatrolAutoCenter* center, uint32_t now, int32_t hold_mdeg);
// 撤销本地回中请求并重置其阶段状态。用于远控或标定接管；不直接写驱动器，后续运行调度决定实际目标。
void PatrolAutoCenter_Cancel(PatrolAutoCenter* center);
/* Return one only while a locally owned zero-angle target may execute.
 * Parking the drive executor, safety arbitration and CAN ownership remain
 * the runtime's responsibility. A rejected/expired return never rearms. */
// 推进本地回中的等待、运动和到位确认阶段。要求安全门禁和行走停放状态，结合新鲜双角度反馈确认到位；失败保留 failed 供运行层故障处理。
uint8_t PatrolAutoCenter_Tick(PatrolAutoCenter* center, uint32_t now,
    uint8_t safe, uint8_t drive_parked, const ChassisFeedback* feedback);

#endif
