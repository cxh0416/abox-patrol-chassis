#ifndef ABSOLUTE_ENCODER_H
#define ABSOLUTE_ENCODER_H

#include <stdint.h>

#define ABS_ENCODER_ADDRESS 1U
#define ABS_ENCODER_REQUEST_SIZE 8U
#define ABS_ENCODER_RESPONSE_SIZE 9U
#define ABS_ENCODER_CONFIG_RESPONSE_SIZE 11U
#define ABS_ENCODER_WRITE_RESPONSE_SIZE 8U

// 计算绝对编码器 Modbus RTU 报文的 CRC16。对给定字节区间逐字节计算，字节排列由构造和解析函数按协议处理。
uint16_t AbsoluteEncoder_Crc16(const uint8_t* data, uint16_t length);
// 构造读取指定保持寄存器区间的 RTU 请求。写入从站、功能码、寄存器起址、数量和 CRC；调用方提供完整请求缓冲区，函数不发送串口。
void AbsoluteEncoder_BuildReadHoldingRequest(uint16_t register_offset, uint16_t register_count,
                                             uint8_t request[ABS_ENCODER_REQUEST_SIZE]);
// 构造读取绝对位置计数的标准请求。复用保持寄存器读取封装，实际串口收发与超时由编码器任务负责。
void AbsoluteEncoder_BuildReadRequest(uint8_t request[ABS_ENCODER_REQUEST_SIZE]);
// 校验保持寄存器读取响应并提取寄存器值。检查站号、功能码、字节数、帧长度及 CRC，失败时不把无效响应当成新反馈。
uint8_t AbsoluteEncoder_ParseHoldingResponse(const uint8_t* response, uint16_t length,
                                             uint16_t* registers, uint16_t register_count);
// 解析编码器位置响应为完整计数。先校验寄存器响应，再按设备寄存器组合规则输出计数，返回解析成功标志。
uint8_t AbsoluteEncoder_ParseResponse(const uint8_t* response, uint16_t length, uint32_t* count);
// 构造单个保持寄存器写入请求。使用 RTU 单写功能码并附 CRC，不能据构造成功认为编码器已保存参数。
void AbsoluteEncoder_BuildWriteSingleRequest(uint16_t register_offset, uint16_t value,
                                             uint8_t request[ABS_ENCODER_REQUEST_SIZE]);
// 验证编码器单寄存器写入回显。逐项核对寄存器地址、写值、长度和 CRC；仅证明该次回显匹配，后续仍按流程复读配置。
uint8_t AbsoluteEncoder_ParseWriteSingleResponse(const uint8_t* response, uint16_t length,
                                                 uint16_t register_offset, uint16_t value);

#endif
