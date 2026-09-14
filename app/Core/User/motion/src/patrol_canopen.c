#include "patrol_canopen.h"

#include <string.h>

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

PatrolCanFrame PatrolCanopen_SdoRead(uint8_t node, uint16_t index, uint8_t subindex)
{
    return sdo_base(node, 0x40U, index, subindex);
}

PatrolCanFrame PatrolCanopen_SdoWrite8(uint8_t node, uint16_t index, uint8_t subindex, uint8_t value)
{
    PatrolCanFrame frame = sdo_base(node, 0x2FU, index, subindex);
    frame.data[4] = value;
    return frame;
}

PatrolCanFrame PatrolCanopen_SdoWrite16(uint8_t node, uint16_t index, uint8_t subindex, uint16_t value)
{
    PatrolCanFrame frame = sdo_base(node, 0x2BU, index, subindex);
    frame.data[4] = (uint8_t)value;
    frame.data[5] = (uint8_t)(value >> 8);
    return frame;
}

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
