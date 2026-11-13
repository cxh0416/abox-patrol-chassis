/* GD board entry: product policy and safety services use the shared ports. */
#include "patrol_gd32_compat.h"
#include "abox_board.h"
#include "abox_can_port.h"
#include "abox_serial_port.h"
#include "abox_watchdog_port.h"
#include "app_log.h"
#include "app_version.h"
#include "boot_cfg.h"
#include "cloud_service.h"
#include "patrol_config.h"
#include "patrol_rtos.h"
#include "remote_service.h"
#include "time_base.h"
#include "vehicle_power_port.h"
#include "cmsis_os.h"

void Error_Handler(void) { __disable_irq(); for (;;) {} }

int main(void)
{
    static const ABoxCanOptions can_options = {1U, 1U, 1U};
    if (!ABoxBoard_Init(APP_START_ADDR)) Error_Handler();
    if (!ABoxSerialPort_Deinit()) Error_Handler();
    ABoxSerialPort_SetConfig(19200U);
    if (!ABoxSerialPort_Init() || !ABoxWatchdog_Init(64U, 1250U)) Error_Handler();
    AppLog_Init();
    AppLog_FieldStartup(Patrol_AppFwVersion());
    TimeBase_Init();
    PatrolConfig_Load();
    VehiclePowerPort_Init();
    if (!ABoxCanPort_InitWithOptions(0U, 500000U, ABOX_CAN_NORMAL, &can_options) ||
        !ABoxCanPort_InitWithOptions(1U, 500000U, ABOX_CAN_NORMAL, &can_options)) Error_Handler();
    CloudService_Init();
    RemoteService_Init();
    RemoteService_CollisionInputs(0U, 0U, HAL_GetTick());
    if (osKernelInitialize() != osOK) Error_Handler();
    PatrolRtos_Init();
    if (osKernelStart() != osOK) Error_Handler();
    Error_Handler();
    return 0;
}
