#include "vehicle_power_port.h"

#include "app_log.h"
#include "abox_board_io_stm32f105.h"
#include "vehicle_power_output.h"

static ABoxBoardIo g_board_io;

#if PATROL_CHASSIS_POWER_OUTPUT_ENABLED
// 通过公共板级 IO 提交 OUT1 动力输出命令。记录请求及端口结果，返回是否写入成功，不把 GPIO 结果当成接触器实测反馈。
static uint8_t write_power_output(uint8_t on)
{
    ABoxBoardIoResult result = ABoxBoardIo_OutputSet(&g_board_io, ABOX_BOARD_IO_OUT1, on);
    APP_LOGI("PWR", "vehicle output commanded=%u result=%u",
             (unsigned int)!!on, (unsigned int)result);
    return result == ABOX_BOARD_IO_OK ? 1U : 0U;
}
#endif

// 按编译许可初始化公共板级 IO 并绑定动力输出。只有明确启用的 OUT1 可写，初始化失败或输出禁用时绑定空回调，保持不可开启状态。
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

// 按当前毫秒时间轮询板级 IO。由公共服务维护输入等状态，不在此授予底盘运动使能。
void VehiclePowerPort_Poll(uint32_t now_ms)
{
    ABoxBoardIo_Poll(&g_board_io, now_ms);
}

// 取得公共板级 IO 输入快照。返回端口结果枚举，调用者必须区分有效采样与未配置反馈，不能凭空推导接触器状态。
ABoxBoardIoResult VehiclePowerPort_InputSnapshot(ABoxBoardIoInputs *inputs)
{
    return ABoxBoardIo_InputsGet(&g_board_io, inputs);
}
