#ifndef PATROL_CAN_REMOTE_H
#define PATROL_CAN_REMOTE_H
#include <stdint.h>
#include "patrol_canopen.h"

#define PATROL_CAN_REMOTE_COMMAND 0x520U
#define PATROL_CAN_REMOTE_ACK 0x521U
#define PATROL_CAN_REMOTE_ANGLES 0x522U
#define PATROL_CAN_REMOTE_WHEELS 0x523U
#define PATROL_CAN_REMOTE_VERSION 1U
#define PATROL_CAN_REMOTE_LEASE_MS 300U

enum { PATROL_CAN_QUERY, PATROL_CAN_ARM, PATROL_CAN_TARGET, PATROL_CAN_STOP,
       PATROL_CAN_POWER_OFF, PATROL_CAN_POWER_ON, PATROL_CAN_RESET_FAULT };
enum { PATROL_CAN_OK, PATROL_CAN_INVALID, PATROL_CAN_SAFETY, PATROL_CAN_OWNER,
       PATROL_CAN_SEQUENCE, PATROL_CAN_NOT_ARMED, PATROL_CAN_STALE, PATROL_CAN_UNAVAILABLE };

void PatrolCanRemote_Init(void);
/* Caller holds the product control critical section. CAN2 only, standard data
 * frames with DLC=8. Never forward raw CANopen writes to the drive bus. */
void PatrolCanRemote_OnFrame(const PatrolCanFrame* frame, uint32_t received_ms, uint32_t now_ms);
void PatrolCanRemote_Tick(uint32_t now_ms, uint8_t transport_healthy);
/* Builds one correlated three-frame response; never grants motion. */
uint8_t PatrolCanRemote_TakeReply(PatrolCanFrame frames[3], uint32_t now_ms);
#endif
