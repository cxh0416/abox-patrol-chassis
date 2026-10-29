#ifndef PATROL_CHASSIS_REMOTE_SERVICE_H
#define PATROL_CHASSIS_REMOTE_SERVICE_H

#include <stdint.h>

#include "chassis_control.h"

// 在临界区撤销共享控制会话和运动目标。清除使能、会话标识、行走和转向目标；只操作控制状态，驱动器停车由运动任务推进。
void RemoteService_Revoke(void);
/* Fail-closed network boundary. No authorization, session identity, enable
 * request or motion target may survive an MQTT runtime switch. */
// 在网络身份或 MQTT 运行配置变化前撤销控制权。同时清空请求重放缓存，防止旧连接的授权和缓存结果跨网络边界继续生效。
void RemoteService_RevokeForNetworkChange(void);
// 格式化用于诊断网络切换安全性的控制状态。输出会话、目标、使能与重放缓存标志，检查 snprintf 截断；不改变控制状态。
uint8_t RemoteService_FormatNetworkState(char *output, uint16_t size);
#if PATROL_CHASSIS_MQTT_LAB_TEST
// 在专用实验构建中植入可检查的旧网络状态。用于验证网络切换是否清除会话、目标和重放缓存；仅受实验编译开关控制，不作为正常控制入口。
void RemoteService_LabSeedNetworkState(void);
#endif
// 将执行失败交给先撤使能后锁存流程；pending 阶段立即禁止新运动，已有首因保持不变。
void RemoteService_ExecutionFault(void);
// 以自动回中边界等已确认的安全检查原因进入统一撤使能和锁存流程，保留底层首因而非覆盖为泛化显式停车。
void RemoteService_SafetyFault(ChassisStopReason reason);
// 在撤使能确认或有界失败退路处理后锁存 MCU 故障；不恢复控制授权。
void RemoteService_CompleteFaultStop(void);
// 初始化共享远控仲裁及请求缓存。根据 Config V5 填入标定有效性，保留内部行走禁用位并清空会话和重放记录；不会自动使能。
void RemoteService_Init(void);
// 在远控任务上下文中推进安全检查。未初始化时返回，实验保持状态受编译开关限制；正常路径持临界区调用安全周期处理。
void RemoteService_Task(void);
// 按指定单调时间推进控制层安全与租约状态。通过可嵌套临界区串行化控制状态，调用统一底盘 Tick 处理门禁、租约和换向确认。
void RemoteService_SafetyTick(uint32_t now_ms);
// 向已初始化的控制层发布最新反馈快照。空指针被忽略；上层须按运行层并发约定组织一致快照与访问保护。
void RemoteService_UpdateFeedback(const ChassisFeedback* feedback);
/* Internal traction inhibit; independent of handset buttons and steering. */
// 设置受临界区保护的内部行走禁用位。只限制行走并取消待换向目标，不撤销转向会话；当前 T12 B 键不调用此接口。
void RemoteService_SetDriveBrake(uint8_t active);
/* Local transports share the same owner, safety checks and lease watchdog.
 * No network clock or JSON envelope is required on the physical CAN port. */
// 处理实体遥控和 CAN2 等本地控制请求。与云端共用会话归属、租约和安全门禁；stop 保持可执行，其他动作不能在标定占用或安全条件不满足时进入。
uint8_t RemoteService_ApplyLocal(const char* action, const char* session_id,
    uint32_t seq, uint32_t lease_ms, int16_t rpm, int32_t steering_mdeg, uint32_t now_ms);
// 解析并执行公共 MQTT 底盘控制请求。核验身份、规范化摘要、重放、可信时钟和有效期后仲裁；有效 stop 和下电优先撤目标，响应只表明处理结果而非物理执行完成。
uint8_t RemoteService_HandleControl(const char* payload, uint16_t length, char* response, uint16_t size);
// 返回共享底盘控制上下文的只读视图。指针指向模块静态存储，会被其他任务更新；跨字段一致性读取需遵守临界区约定，不得保存为独立历史快照。
const ChassisControl* RemoteService_State(void);

#endif
