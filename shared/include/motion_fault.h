#ifndef PATROL_MOTION_FAULT_H
#define PATROL_MOTION_FAULT_H
#include <stdint.h>
/* Fixed, versioned flash journal record. CRC/commit word is written last. */
typedef struct {
 uint32_t magic, sequence, at_ms, source, reason, gates;
 int32_t target_rpm, target_mdeg, left_rpm, right_rpm, driver_raw;
 uint32_t encoder_raw, steer_sw, drive_sw, left_code, right_code;
 uint32_t left_code_age, right_code_age, steer_age, drive_age;
 uint32_t bench_phase, bench_step, bench_fault, drive_phase, drive_step, drive_fault;
 uint32_t last_abort, encoder_error, center_phase, reserved[2], checksum;
} MotionFaultRecord;
_Static_assert(sizeof(MotionFaultRecord)==128, "fault journal layout");
uint8_t OTA_Info_LoadMotionFault(MotionFaultRecord* record);
uint8_t OTA_Info_SaveMotionFault(const MotionFaultRecord* record);
#endif
