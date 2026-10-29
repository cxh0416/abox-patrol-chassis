#ifndef PATROL_CANOPEN_H
#define PATROL_CANOPEN_H

#include <stdint.h>

#define PATROL_CANOPEN_STEERING_NODE 1U
#define PATROL_CANOPEN_DRIVE_NODE 2U

typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} PatrolCanFrame;

// 构造指定节点对象字典的 SDO 上传请求。设置 COB-ID、索引和子索引，返回值是待发送帧，不含发送或超时处理。
PatrolCanFrame PatrolCanopen_SdoRead(uint8_t node, uint16_t index, uint8_t subindex);
// 构造 8 位对象值的 expedited SDO 下载请求。采用协议指定长度标志；调用者须等待对应索引的驱动器响应。
PatrolCanFrame PatrolCanopen_SdoWrite8(uint8_t node, uint16_t index, uint8_t subindex, uint8_t value);
// 构造 16 位对象值的 expedited SDO 下载请求。按小端顺序编码载荷，不进行模式或使能状态仲裁。
PatrolCanFrame PatrolCanopen_SdoWrite16(uint8_t node, uint16_t index, uint8_t subindex, uint16_t value);
// 构造 32 位对象值的 expedited SDO 下载请求。保留带符号数值的位模式，右轮方向转换由上层完成。
PatrolCanFrame PatrolCanopen_SdoWrite32(uint8_t node, uint16_t index, uint8_t subindex, int32_t value);
// 校验 expedited SDO 响应并提取值及宽度。核对节点、索引、子索引和命令字，输出实际字节数；调用者进一步验证对象预期宽度与值域。
uint8_t PatrolCanopen_ParseSdoValue(const PatrolCanFrame* frame, uint8_t node, uint16_t index,
                                    uint8_t subindex, int32_t* value, uint8_t* size);

#endif

