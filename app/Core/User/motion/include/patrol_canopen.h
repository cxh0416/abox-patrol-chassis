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

PatrolCanFrame PatrolCanopen_SdoRead(uint8_t node, uint16_t index, uint8_t subindex);
PatrolCanFrame PatrolCanopen_SdoWrite8(uint8_t node, uint16_t index, uint8_t subindex, uint8_t value);
PatrolCanFrame PatrolCanopen_SdoWrite16(uint8_t node, uint16_t index, uint8_t subindex, uint16_t value);
PatrolCanFrame PatrolCanopen_SdoWrite32(uint8_t node, uint16_t index, uint8_t subindex, int32_t value);
uint8_t PatrolCanopen_ParseSdoValue(const PatrolCanFrame* frame, uint8_t node, uint16_t index,
                                    uint8_t subindex, int32_t* value, uint8_t* size);

#endif

