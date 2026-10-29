#ifndef PATROL_CHASSIS_FIXED_POINT_H
#define PATROL_CHASSIS_FIXED_POINT_H

#include <stdint.h>

// 将千分之一单位的有符号整数编码为十进制文本。避免为毫度等值引入浮点格式化，输出受缓冲容量限制，返回格式化有效标志。
uint8_t FixedPoint_FormatMilli(char* output, uint16_t size, int32_t milli_value);

#endif
