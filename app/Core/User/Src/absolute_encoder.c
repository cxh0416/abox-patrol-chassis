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

void AbsoluteEncoder_BuildReadRequest(uint8_t request[ABS_ENCODER_REQUEST_SIZE])
{
    uint16_t crc;
    request[0] = ABS_ENCODER_ADDRESS;
    request[1] = 0x03U;
    request[2] = 0x00U;
    request[3] = 0x03U; /* 40004, zero-based register offset 3 */
    request[4] = 0x00U;
    request[5] = 0x02U;
    crc = AbsoluteEncoder_Crc16(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);
}

uint8_t AbsoluteEncoder_ParseResponse(const uint8_t* response, uint16_t length, uint32_t* count)
{
    uint16_t expected;
    uint16_t actual;
    uint16_t low_word;
    uint16_t high_word;
    if (response == 0 || count == 0 || length != ABS_ENCODER_RESPONSE_SIZE ||
        response[0] != ABS_ENCODER_ADDRESS || response[1] != 0x03U || response[2] != 0x04U) return 0U;
    expected = AbsoluteEncoder_Crc16(response, 7U);
    actual = (uint16_t)response[7] | (uint16_t)response[8] << 8;
    if (expected != actual) return 0U;
    low_word = (uint16_t)response[3] << 8 | response[4];
    high_word = (uint16_t)response[5] << 8 | response[6];
    *count = (uint32_t)low_word | (uint32_t)high_word << 16;
    return 1U;
}

