#include <stddef.h>
#include "motion_internal.h"
#include "chassis_control.h"
#include "abox_log_ring.h"
#include "can.h"

#define OFF(t,f) offsetof(t,f)
const unsigned target_layout[] = {
    sizeof(PatrolMotion), OFF(PatrolMotion,bench), OFF(PatrolMotion,drive), OFF(PatrolMotion,feedback_gate_mask),
    sizeof(BenchSteering), OFF(BenchSteering,index), OFF(BenchSteering,sent_ms), OFF(BenchSteering,expected),
    sizeof(BenchDrive), OFF(BenchDrive,writing),
    sizeof(ChassisControl), OFF(ChassisControl,stop_reason),
    sizeof(CAN_HandleTypeDef), OFF(CAN_HandleTypeDef,ErrorCode),
    sizeof(ABoxLogRing), OFF(ABoxLogRing,ticks),
    sizeof(MotionFaultRecord), OFF(MotionFaultRecord,checksum)
};
