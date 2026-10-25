#include "absolute_encoder.h"
#include <string.h>

// 计算绝对编码器 Modbus RTU 报文的 CRC16。对给定字节区间逐字节计算，字节排列由构造和解析函数按协议处理。
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

// 构造读取指定保持寄存器区间的 RTU 请求。写入从站、功能码、寄存器起址、数量和 CRC；调用方提供完整请求缓冲区，函数不发送串口。
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

// 构造读取绝对位置计数的标准请求。复用保持寄存器读取封装，实际串口收发与超时由编码器任务负责。
void AbsoluteEncoder_BuildReadRequest(uint8_t request[ABS_ENCODER_REQUEST_SIZE])
{
    AbsoluteEncoder_BuildReadHoldingRequest(3U, 2U, request);
}

// 校验保持寄存器读取响应并提取寄存器值。检查站号、功能码、字节数、帧长度及 CRC，失败时不把无效响应当成新反馈。
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

// 在接收缓冲区中搜索有效保持寄存器响应，兼容半双工总线把请求回显放在响应之前的情况。
uint8_t AbsoluteEncoder_ParseHoldingResponseAnywhere(const uint8_t* bytes, uint16_t length,
                                                     uint16_t* registers, uint16_t register_count)
{
    uint16_t expected_length;
    uint16_t offset;
    if (bytes == 0 || registers == 0 || register_count == 0U) return 0U;
    expected_length = (uint16_t)(5U + register_count * 2U);
    if (length < expected_length) return 0U;
    for (offset = 0U; offset <= (uint16_t)(length - expected_length); offset++) {
        if (bytes[offset] == ABS_ENCODER_ADDRESS && bytes[offset + 1U] == 0x03U &&
            bytes[offset + 2U] == (uint8_t)(register_count * 2U) &&
            AbsoluteEncoder_ParseHoldingResponse(bytes + offset, expected_length,
                                                  registers, register_count))
            return 1U;
    }
    return 0U;
}

// 解析编码器位置响应为完整计数。先校验寄存器响应，再按设备寄存器组合规则输出计数，返回解析成功标志。
uint8_t AbsoluteEncoder_ParseResponse(const uint8_t* response, uint16_t length, uint32_t* count)
{
    uint16_t registers[2];
    if (count == 0 ||
        !AbsoluteEncoder_ParseHoldingResponseAnywhere(response, length, registers, 2U)) return 0U;
    *count = (uint32_t)registers[0] | (uint32_t)registers[1] << 16;
    return 1U;
}

// 构造单个保持寄存器写入请求。使用 RTU 单写功能码并附 CRC，不能据构造成功认为编码器已保存参数。
void AbsoluteEncoder_BuildWriteSingleRequest(uint16_t register_offset, uint16_t value,
                                             uint8_t request[ABS_ENCODER_REQUEST_SIZE])
{
    uint16_t crc;
    request[0] = ABS_ENCODER_ADDRESS;
    request[1] = 0x06U;
    request[2] = (uint8_t)(register_offset >> 8);
    request[3] = (uint8_t)register_offset;
    request[4] = (uint8_t)(value >> 8);
    request[5] = (uint8_t)value;
    crc = AbsoluteEncoder_Crc16(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);
}

// 验证编码器单寄存器写入回显。逐项核对寄存器地址、写值、长度和 CRC；仅证明该次回显匹配，后续仍按流程复读配置。
uint8_t AbsoluteEncoder_ParseWriteSingleResponse(const uint8_t* response, uint16_t length,
                                                 uint16_t register_offset, uint16_t value)
{
    uint8_t expected[ABS_ENCODER_REQUEST_SIZE];
    uint16_t offset;
    if (response == 0 || length < ABS_ENCODER_WRITE_RESPONSE_SIZE) return 0U;
    AbsoluteEncoder_BuildWriteSingleRequest(register_offset, value, expected);
    if (length == ABS_ENCODER_WRITE_RESPONSE_SIZE)
        return memcmp(response, expected, ABS_ENCODER_WRITE_RESPONSE_SIZE) == 0 ? 1U : 0U;
    /* A write echo can contain the identical request twice. Only a matching
     * frame after the leading request proves that the device answered. */
    for (offset = ABS_ENCODER_REQUEST_SIZE;
         offset <= (uint16_t)(length - ABS_ENCODER_WRITE_RESPONSE_SIZE); offset++) {
        if (memcmp(response + offset, expected, ABS_ENCODER_WRITE_RESPONSE_SIZE) == 0)
            return 1U;
    }
    return 0U;
}
