#include "fixed_point.h"

#include <stdio.h>

// 将千分之一单位的有符号整数编码为十进制文本。避免为毫度等值引入浮点格式化，输出受缓冲容量限制，返回格式化有效标志。
uint8_t FixedPoint_FormatMilli(char* output, uint16_t size, int32_t milli_value)
{
    int64_t magnitude;
    int written;
    if (output == 0 || size == 0U) return 0U;
    magnitude = milli_value;
    if (magnitude < 0) magnitude = -magnitude;
    written = snprintf(output, size, "%s%ld.%03ld", milli_value < 0 ? "-" : "",
                       (long)(magnitude / 1000), (long)(magnitude % 1000));
    return (uint8_t)(written > 0 && written < size);
}
