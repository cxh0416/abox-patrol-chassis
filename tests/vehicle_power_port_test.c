#include <assert.h>
#include <stdarg.h>
#include <stdint.h>

#include "abox_board_io_stm32f105.h"
#include "app_log.h"
#include "vehicle_power_output.h"
#include "vehicle_power_port.h"

static uint8_t output[4], led[3], fail_prepare;

void AppLog_Printf(AppLogLevel_t level, const char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; }
static int prepare(void *context)
{
    unsigned i;
    (void)context;
    for (i = 0; i < 4; ++i) output[i] = 0;
    for (i = 0; i < 3; ++i) led[i] = 0;
    return !fail_prepare;
}
static int write_output(void *context, ABoxBoardIoOutput channel, uint8_t on)
{ (void)context; output[channel] = on; return 1; }
static int read_output(void *context, ABoxBoardIoOutput channel, uint8_t *on)
{ (void)context; *on = output[channel]; return 1; }
static int read_input(void *context, ABoxBoardIoInput channel, uint8_t *on)
{ (void)context; *on = (uint8_t)(channel & 1U); return 1; }
static int write_led(void *context, ABoxBoardIoLed channel, uint8_t on)
{ (void)context; led[channel] = on; return 1; }
static uint32_t enter(void *context) { (void)context; return 0; }
static void leave(void *context, uint32_t key) { (void)context; (void)key; }
const ABoxBoardIoPort *ABoxBoardIoStm32_Port(void)
{
    static const ABoxBoardIoPort port = {0, prepare, write_output, read_output,
                                         read_input, write_led, enter, leave, 0};
    return &port;
}
int main(void)
{
    ABoxBoardIoInputs inputs;
    unsigned i;
    fail_prepare = 1U;
    VehiclePowerPort_Init();
    assert(!VehiclePowerOutput_IsAvailable());
    assert(!VehiclePowerOutput_Set(1U));
    fail_prepare = 0U;
    VehiclePowerPort_Init();
#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
    assert(VehiclePowerOutput_IsAvailable());
    assert(VehiclePowerOutput_Set(1U));
    assert(output[ABOX_BOARD_IO_OUT1] == 1U && VehiclePowerOutput_IsOn());
    assert(VehiclePowerOutput_Set(0U));
#else
    assert(!VehiclePowerOutput_IsAvailable());
    assert(!VehiclePowerOutput_Set(1U));
#endif
    for (i = 0; i < 4; ++i) assert(output[i] == 0U);
    VehiclePowerPort_Poll(1U);
    VehiclePowerPort_Poll(21U);
    assert(VehiclePowerPort_InputSnapshot(&inputs) == ABOX_BOARD_IO_OK);
    assert(inputs.valid_mask == 0xffU && inputs.stable_mask == 0xaaU);
    return 0;
}
