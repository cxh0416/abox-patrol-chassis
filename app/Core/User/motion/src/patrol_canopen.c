#include "patrol_canopen.h"

#include <string.h>

// 构造 SDO 请求公共头并清空载荷。按节点生成请求 COB-ID，填入调用方传入的读写命令字、索引和子索引，固定载荷长度为 8 字节。
static PatrolCanFrame sdo_base(uint8_t node, uint8_t command, uint16_t index, uint8_t subindex)
{
    PatrolCanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = (uint16_t)(0x600U + node);
    frame.dlc = 8U;
    frame.data[0] = command;
    frame.data[1] = (uint8_t)index;
    frame.data[2] = (uint8_t)(index >> 8);
    frame.data[3] = subindex;
    return frame;
}

// 构造指定节点对象字典的 SDO 上传请求。设置 COB-ID、索引和子索引，返回值是待发送帧，不含发送或超时处理。
PatrolCanFrame PatrolCanopen_SdoRead(uint8_t node, uint16_t index, uint8_t subindex)
{
    return sdo_base(node, 0x40U, index, subindex);
}

// 构造 8 位对象值的 expedited SDO 下载请求。采用协议指定长度标志；调用者须等待对应索引的驱动器响应。
PatrolCanFrame PatrolCanopen_SdoWrite8(uint8_t node, uint16_t index, uint8_t subindex, uint8_t value)
{
    PatrolCanFrame frame = sdo_base(node, 0x2FU, index, subindex);
    frame.data[4] = value;
    return frame;
}

// 构造 16 位对象值的 expedited SDO 下载请求。按小端顺序编码载荷，不进行模式或使能状态仲裁。
PatrolCanFrame PatrolCanopen_SdoWrite16(uint8_t node, uint16_t index, uint8_t subindex, uint16_t value)
{
    PatrolCanFrame frame = sdo_base(node, 0x2BU, index, subindex);
    frame.data[4] = (uint8_t)value;
    frame.data[5] = (uint8_t)(value >> 8);
    return frame;
}

// 构造 32 位对象值的 expedited SDO 下载请求。保留带符号数值的位模式，右轮方向转换由上层完成。
PatrolCanFrame PatrolCanopen_SdoWrite32(uint8_t node, uint16_t index, uint8_t subindex, int32_t value)
{
    PatrolCanFrame frame = sdo_base(node, 0x23U, index, subindex);
    uint32_t raw = (uint32_t)value;
    frame.data[4] = (uint8_t)raw;
    frame.data[5] = (uint8_t)(raw >> 8);
    frame.data[6] = (uint8_t)(raw >> 16);
    frame.data[7] = (uint8_t)(raw >> 24);
    return frame;
}

// 校验 expedited SDO 响应并提取值及宽度。核对节点、索引、子索引和命令字，输出实际字节数；调用者进一步验证对象预期宽度与值域。
uint8_t PatrolCanopen_ParseSdoValue(const PatrolCanFrame* frame, uint8_t node, uint16_t index,
                                    uint8_t subindex, int32_t* value, uint8_t* size)
{
    uint8_t width;
    uint32_t raw;
    if (frame == 0 || value == 0 || size == 0 || frame->id != (uint16_t)(0x580U + node) ||
        frame->dlc < 4U || frame->dlc > 8U || frame->data[1] != (uint8_t)index ||
        frame->data[2] != (uint8_t)(index >> 8) || frame->data[3] != subindex) return 0U;
    if (frame->data[0] == 0x4FU) width = 1U;
    else if (frame->data[0] == 0x4BU) width = 2U;
    else if (frame->data[0] == 0x47U) width = 3U;
    else if (frame->data[0] == 0x43U) width = 4U;
    else return 0U;
    /* VSY replies omit unused padding (e.g. 6041 is DLC 6).  Require
     * every declared value byte, while accepting padded CANopen replies. */
    if (frame->dlc < (uint8_t)(4U + width)) return 0U;
    raw = frame->data[4];
    if (width >= 2U) raw |= (uint32_t)frame->data[5] << 8;
    if (width >= 3U) raw |= (uint32_t)frame->data[6] << 16;
    if (width == 4U) raw |= (uint32_t)frame->data[7] << 24;
    *value = (int32_t)raw;
    *size = width;
    return 1U;
}
