#include "abox_product_config.h"
/* GD board entry: product policy and safety services use the shared ports. */
#include "patrol_gd32_compat.h"
#include "abox_board.h"
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
    static const ABoxBoardUartConfig uarts[] = {
        {ABOX_BOARD_UART_MODEM, {ABOX_PRODUCT_MODEM_UART_BAUD, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}},
        {ABOX_BOARD_UART_BUS, {ABOX_PRODUCT_BUS_UART_BAUD, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC}}
    };
    static const ABoxMcuClockConfig clock = ABOX_PRODUCT_CLOCK_CONFIG;
    static const ABoxBoardConfig board = {APP_START_ADDR, uarts, sizeof(uarts) / sizeof(uarts[0]), &clock};
    if (!ABoxBoard_Init(&board) || !ABoxWatchdog_Init(64U, 1250U)) Error_Handler();
    AppLog_Init();
    AppLog_FieldStartup(Patrol_AppFwVersion());
    TimeBase_Init();
    PatrolConfig_Load();
    VehiclePowerPort_Init();
    CloudService_Init();
    RemoteService_Init();
    RemoteService_CollisionInputs(0U, 0U, HAL_GetTick());
    if (osKernelInitialize() != osOK) Error_Handler();
    PatrolRtos_Init();
    if (osKernelStart() != osOK) Error_Handler();
    Error_Handler();
    return 0;
}
