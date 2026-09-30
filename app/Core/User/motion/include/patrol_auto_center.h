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

void PatrolAutoCenter_Request(PatrolAutoCenter* center, uint32_t now, int32_t hold_mdeg);
void PatrolAutoCenter_Cancel(PatrolAutoCenter* center);
/* Return one only while a locally owned zero-angle target may execute.
 * Parking the drive executor, safety arbitration and CAN ownership remain
 * the runtime's responsibility. A rejected/expired return never rearms. */
uint8_t PatrolAutoCenter_Tick(PatrolAutoCenter* center, uint32_t now,
    uint8_t safe, uint8_t drive_parked, const ChassisFeedback* feedback);

#endif
