#include "absolute_encoder.h"

uint16_t AbsoluteEncoder_Crc16(const uint8_t* data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    for (i = 0U; i < length; ++i) {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

void AbsoluteEncoder_BuildReadHoldingRequest(uint16_t register_offset, uint16_t register_count,
                                             uint8_t request[ABS_ENCODER_REQUEST_SIZE])
{
    uint16_t crc;
    request[0] = ABS_ENCODER_ADDRESS;
    request[1] = 0x03U;
    request[2] = (uint8_t)(register_offset >> 8);
    request[3] = (uint8_t)register_offset;
    request[4] = (uint8_t)(register_count >> 8);
    request[5] = (uint8_t)register_count;
    crc = AbsoluteEncoder_Crc16(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);
}

void AbsoluteEncoder_BuildReadRequest(uint8_t request[ABS_ENCODER_REQUEST_SIZE])
{
    AbsoluteEncoder_BuildReadHoldingRequest(3U, 2U, request);
}

uint8_t AbsoluteEncoder_ParseHoldingResponse(const uint8_t* response, uint16_t length,
                                             uint16_t* registers, uint16_t register_count)
{
    uint16_t expected;
    uint16_t actual;
    uint16_t index;
    uint16_t expected_length = (uint16_t)(5U + register_count * 2U);
    if (response == 0 || registers == 0 || register_count == 0U || length != expected_length ||
        response[0] != ABS_ENCODER_ADDRESS || response[1] != 0x03U ||
        response[2] != (uint8_t)(register_count * 2U)) return 0U;
    expected = AbsoluteEncoder_Crc16(response, (uint16_t)(length - 2U));
    actual = (uint16_t)response[length - 2U] | (uint16_t)response[length - 1U] << 8;
    if (expected != actual) return 0U;
    for (index = 0U; index < register_count; index++)
    {
        registers[index] = (uint16_t)response[3U + index * 2U] << 8 |
                           response[4U + index * 2U];
    }
    return 1U;
}

uint8_t AbsoluteEncoder_ParseResponse(const uint8_t* response, uint16_t length, uint32_t* count)
{
    uint16_t registers[2];
    if (count == 0 || !AbsoluteEncoder_ParseHoldingResponse(response, length, registers, 2U)) return 0U;
    *count = (uint32_t)registers[0] | (uint32_t)registers[1] << 16;
    return 1U;
}
