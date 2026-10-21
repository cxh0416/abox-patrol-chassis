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

// 清空 CAN2 命令入口序号、武装和待响应状态。初始化不依赖 MQTT 或 UTC，仍须在后续 ARM 命令中通过安全门禁。
void PatrolCanRemote_Init(void);
/* Caller holds the product control critical section. CAN2 only, standard data
 * frames with DLC=8. Never forward raw CANopen writes to the drive bus. */
// 解析 CAN2 本地控制命令并执行仲裁。校验帧格式、版本、序号和接收年龄，分别处理查询、供电、使能、目标、停止及复位；运动权限仍由共享服务决定。
void PatrolCanRemote_OnFrame(const PatrolCanFrame* frame, uint32_t received_ms, uint32_t now_ms);
// 监督 CAN2 远控租约与底层通信状态。在失联或通信故障时撤销会话，故障按现有安全仲裁记录；不得借续租延续陈旧目标。
void PatrolCanRemote_Tick(uint32_t now_ms, uint8_t transport_healthy);
/* Builds one correlated three-frame response; never grants motion. */
// 取出一组关联同一请求序号的三帧响应。将当前结果和安全状态编码到固定响应 ID；没有待响应时返回零，响应成功不表示真实运动完成。
uint8_t PatrolCanRemote_TakeReply(PatrolCanFrame frames[3], uint32_t now_ms);
#endif
