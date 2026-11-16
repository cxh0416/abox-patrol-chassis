#ifndef PATROL_GD32_COMPAT_H
#define PATROL_GD32_COMPAT_H

#include <stdint.h>
#include "gd32f10x.h"
#include "abox_platform_port.h"
#include "abox_mcu.h"
#define HAL_GetTick() ABox_PortGetTickMs()
#define HAL_GetUIDw0() ABoxMcu_UidWord(0U)
#define HAL_GetUIDw1() ABoxMcu_UidWord(1U)

void Error_Handler(void);

#endif
