#ifndef PATROL_CHASSIS_PHY_REMOTE_H
#define PATROL_CHASSIS_PHY_REMOTE_H

#include <stdint.h>

#include "patrol_canopen.h"

/* Physical T12 handset receiver on CAN2 (A8/A9). The steering/drive
 * controllers stay on CAN1 (A6/A7). The receiver publishes two standard
 * data frames:
 *   0x10A switch frame   : byte2 = enable (nonzero = active, 0 = inactive)
 *   0x10B joystick frame : byte0-1 = steering int16, byte4-5 = throttle int16
 * 0x10A byte2 is the handset "channel A" enable switch. Motion is requested
 * through the shared ChassisControl session, so every existing safety gate
 * (calibration, dual-angle agreement, reversal confirmation, node health,
 * Bus-Off, OTA, lease watchdog) still applies.
 * The vehicle power output follows the same enable switch: active (0) powers
 * on before acquiring; inactive powers off after a short grace delay so the
 * bounded stop sequence can finish first. */

#define PATROL_PHY_REMOTE_SWITCH_ID    0x10AU
#define PATROL_PHY_REMOTE_JOYSTICK_ID  0x10BU
#define PATROL_PHY_REMOTE_LEASE_MS     300U

void PatrolPhyRemote_Init(void);

/* Returns 1 when the frame is a physical-remote frame and was consumed by this
 * module. Called under the product control critical section on the CAN2 path. */
uint8_t PatrolPhyRemote_OnFrame(const PatrolCanFrame* frame, uint32_t received_ms, uint32_t now_ms);

/* Caller holds the product control critical section. transport_healthy is the
 * CAN2 (remote bus) non-Bus-Off state. */
void PatrolPhyRemote_Tick(uint32_t now_ms, uint8_t transport_healthy);

#endif
