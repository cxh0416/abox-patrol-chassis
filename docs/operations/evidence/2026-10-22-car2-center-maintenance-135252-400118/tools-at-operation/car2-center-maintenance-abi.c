#include <stddef.h>
#include <stdint.h>
#include "motion_internal.h"
const uint32_t mailbox[3] __attribute__((used,section(".mailbox")))={offsetof(PatrolMotion,center_cal.encoder_write_state),sizeof(((PatrolMotion*)0)->center_cal.encoder_write_state),sizeof(PatrolMotion)};
