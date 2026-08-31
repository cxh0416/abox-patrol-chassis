#ifndef PATROL_CHASSIS_OTA_IMAGE_H
#define PATROL_CHASSIS_OTA_IMAGE_H

#include <stdint.h>

#include "boot_cfg.h"

static inline uint32_t PatrolChassisOta_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static inline uint8_t PatrolChassisOta_VectorValid(const uint8_t *vector)
{
    uint32_t sp;
    uint32_t reset;

    if (vector == 0) return 0U;
    sp = PatrolChassisOta_ReadLe32(vector);
    reset = PatrolChassisOta_ReadLe32(vector + 4U);
    return (sp >= 0x20000000U && sp <= 0x20010000U &&
            (reset & 1U) != 0U && reset >= APP_START_ADDR &&
            reset <= APP_END_ADDR) ? 1U : 0U;
}

#endif
