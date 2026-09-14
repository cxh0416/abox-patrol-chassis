#ifndef ABSOLUTE_ENCODER_H
#define ABSOLUTE_ENCODER_H

#include <stdint.h>

#define ABS_ENCODER_ADDRESS 1U
#define ABS_ENCODER_REQUEST_SIZE 8U
#define ABS_ENCODER_RESPONSE_SIZE 9U
#define ABS_ENCODER_CONFIG_RESPONSE_SIZE 11U
#define ABS_ENCODER_WRITE_RESPONSE_SIZE 8U

uint16_t AbsoluteEncoder_Crc16(const uint8_t* data, uint16_t length);
void AbsoluteEncoder_BuildReadHoldingRequest(uint16_t register_offset, uint16_t register_count,
                                             uint8_t request[ABS_ENCODER_REQUEST_SIZE]);
void AbsoluteEncoder_BuildReadRequest(uint8_t request[ABS_ENCODER_REQUEST_SIZE]);
uint8_t AbsoluteEncoder_ParseHoldingResponse(const uint8_t* response, uint16_t length,
                                             uint16_t* registers, uint16_t register_count);
uint8_t AbsoluteEncoder_ParseResponse(const uint8_t* response, uint16_t length, uint32_t* count);
void AbsoluteEncoder_BuildWriteSingleRequest(uint16_t register_offset, uint16_t value,
                                             uint8_t request[ABS_ENCODER_REQUEST_SIZE]);
uint8_t AbsoluteEncoder_ParseWriteSingleResponse(const uint8_t* response, uint16_t length,
                                                 uint16_t register_offset, uint16_t value);

#endif
