#include "vehicle_power_port.h"

#include "app_log.h"
#include "abox_board_io_stm32f105.h"
#include "vehicle_power_output.h"

static ABoxBoardIo g_board_io;

#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
static uint8_t write_power_output(uint8_t on)
{
    ABoxBoardIoResult result = ABoxBoardIo_OutputSet(&g_board_io, ABOX_BOARD_IO_OUT1, on);
    APP_LOGI("PWR", "vehicle output commanded=%u result=%u",
             (unsigned int)!!on, (unsigned int)result);
    return result == ABOX_BOARD_IO_OK ? 1U : 0U;
}
#endif

void VehiclePowerPort_Init(void)
{
    uint8_t allowed_outputs = 0U;
#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
    allowed_outputs = (uint8_t)(1U << ABOX_BOARD_IO_OUT1);
#endif
    if (ABoxBoardIo_Init(&g_board_io, ABoxBoardIoStm32_Port(), allowed_outputs) != ABOX_BOARD_IO_OK) {
        VehiclePowerOutput_Init(0);
        return;
    }
#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
    VehiclePowerOutput_Init(write_power_output);
#else
    VehiclePowerOutput_Init(0);
#endif
}

void VehiclePowerPort_Poll(uint32_t now_ms)
{
    ABoxBoardIo_Poll(&g_board_io, now_ms);
}

ABoxBoardIoResult VehiclePowerPort_InputSnapshot(ABoxBoardIoInputs *inputs)
{
    return ABoxBoardIo_InputsGet(&g_board_io, inputs);
}
