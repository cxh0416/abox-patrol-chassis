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
// 在必要恢复后读取最新有效运动故障。写入忙时避免递归恢复，找不到有效记录或恢复失败返回零，不能用空内容代替有效证据。
uint8_t OTA_Info_LoadMotionFault(MotionFaultRecord* record);
// 以原子忙标志串行追加一条运动故障记录。必要时先恢复配置日志，再写空槽并复读；输出关闭和 OTA 空闲条件由运行层调用方保证，本函数不检查这些门禁。
uint8_t OTA_Info_SaveMotionFault(const MotionFaultRecord* record);
#endif
