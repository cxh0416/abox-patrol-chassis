#ifndef PATROL_GD32_COMPAT_H
#define PATROL_GD32_COMPAT_H

#include <stdint.h>
#include "gd32f10x.h"
#include "abox_platform_port.h"
#include "abox_mcu.h"
#define HAL_GetTick() ABox_PortGetTickMs()
#define HAL_GetUIDw0() ABoxMcu_UidWord(0U)
#define HAL_GetUIDw1() ABoxMcu_UidWord(1U)

#define RCC_FLAG_IWDGRST RCU_FLAG_FWDGTRST
#define RCC_FLAG_WWDGRST RCU_FLAG_WWDGTRST
#define RCC_FLAG_SFTRST RCU_FLAG_SWRST
#define RCC_FLAG_LPWRRST RCU_FLAG_LPRST
#define RCC_FLAG_PORRST RCU_FLAG_PORRST
#define RCC_FLAG_PINRST RCU_FLAG_EPRST
#define __HAL_RCC_GET_FLAG(flag) (rcu_flag_get(flag) != RESET)
#define __HAL_RCC_CLEAR_RESET_FLAGS() rcu_all_reset_flag_clear()
void Error_Handler(void);

#endif
