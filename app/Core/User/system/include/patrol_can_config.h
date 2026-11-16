#ifndef PATROL_CAN_CONFIG_H
#define PATROL_CAN_CONFIG_H

#include "abox_can_port.h"

/* Both controllers use the same 500 kbit/s timing and recovery policy. */
#define PATROL_CAN_CONFIG {500000U, 15U, 2U, 1U, ABOX_CAN_NORMAL, {1U, 1U, 1U}, {0}}

#endif
