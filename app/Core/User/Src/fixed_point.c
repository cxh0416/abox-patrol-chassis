#include "fixed_point.h"

#include <stdio.h>

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
