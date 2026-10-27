#ifndef CHASSIS_CONTROL_H
#define CHASSIS_CONTROL_H

#include <stdint.h>

#define CHASSIS_MAX_RPM 2000
#define CHASSIS_MAX_STEERING_MDEG 30000
#define CHASSIS_MAX_LEASE_MS 1000U
#define CHASSIS_FEEDBACK_TIMEOUT_MS 250U
#define CHASSIS_STEERING_PAIR_SKEW_MS 30U

typedef enum {
    CHASSIS_STOP_NONE = 0,
    CHASSIS_STOP_RESERVED_1 = 1,
    CHASSIS_STOP_UNCALIBRATED,
    CHASSIS_STOP_FEEDBACK_STALE,
    CHASSIS_STOP_ANGLE_MISMATCH,
    CHASSIS_STOP_NODE_OFFLINE,
    CHASSIS_STOP_CAN_BUS_OFF,
    CHASSIS_STOP_DRIVE_FAULT,
    CHASSIS_STOP_OTA_BUSY,
    CHASSIS_STOP_POWER_NOT_READY,
    CHASSIS_STOP_LEASE_EXPIRED,
    CHASSIS_STOP_DIRECTION_TIMEOUT,
    CHASSIS_STOP_EXPLICIT,
    CHASSIS_STOP_EXECUTION_FAULT,
    CHASSIS_STOP_ENCODER_RANGE,
    CHASSIS_STOP_ENCODER_PROFILE
} ChassisStopReason;

typedef struct {
    uint8_t calibrated;
    uint8_t steering_online;
    uint8_t drive_online;
    uint8_t can_bus_off;
    uint8_t drive_fault;
    uint8_t ota_busy;
    uint8_t power_ready;
    uint8_t encoder_range_valid;
    uint8_t encoder_profile_ready;
    uint8_t angle_pair_valid;
    int16_t left_actual_rpm;
    int16_t right_actual_rpm;
    int32_t steering_actual_mdeg;
    int32_t encoder_actual_mdeg;
    uint32_t feedback_ms;
} ChassisFeedback;

typedef struct {
    char session_id[40];
    uint32_t last_seq;
    uint32_t lease_deadline_ms;
    uint32_t reversal_started_ms;
    uint32_t last_zero_feedback_ms;
    int16_t requested_rpm;
    int16_t target_rpm;
    int32_t target_steering_mdeg;
    int8_t active_direction;
    int8_t steering_boundary; /* -1/+1 inward-only; clear only after operator backs off. */
    uint8_t session_active : 1;
    uint8_t reversal_pending : 1;
    uint8_t zero_confirmations : 2;
    uint8_t enable_request : 1;
    uint8_t fault_latched : 1;
    uint8_t auto_center_inhibited : 1; /* SAFETY_STOP parks until a new accepted command/power cycle. */
    uint8_t drive_brake : 1; /* Traction-only inhibit; steering/session stay authorized. */
    uint8_t fault_pending : 1; /* 已禁止运动，正在优先撤使能，尚未完成故障锁存。 */
    ChassisStopReason stop_reason;
    ChassisFeedback feedback;
    uint8_t (*request_fault_stop)(void);
} ChassisControl;

// 初始化底盘控制上下文到未标定的安全默认状态。清空会话、目标、反馈和换向计数；初始化本身不会使能电机，后续仍须通过反馈安全门禁。
void ChassisControl_Init(ChassisControl* control);
// 安装产品故障停机请求。回调返回非零表示异步撤使能；无执行器的独立控制测试可不安装并保持立即锁存。
void ChassisControl_SetFaultStopHandler(ChassisControl* control, uint8_t (*handler)(void));
// 首次故障立即禁止新运动并调用撤使能流程；保留首因，异步停机完成或达到有界期限后才正式锁存。
void ChassisControl_RequestFault(ChassisControl* control, ChassisStopReason reason);
// 完成故障锁存。调用方须先处理撤使能确认或超时退路；不会恢复任何会话、目标或使能。
void ChassisControl_CompleteFaultStop(ChassisControl* control);
// 设置内部行走禁用状态，不撤销转向授权。激活时立即清零行走与待换向目标，保留最近非零方向以防解除后绕过换向检查；当前不绑定 T12 B 键。
void ChassisControl_SetDriveBrake(ChassisControl* control, uint8_t active);
// 返回阻止正常运动的首个安全原因。在运行期门禁基础上检查标定、编码器规格、量程、电源就绪和双角度偏差；偏差超过 2000 mdeg 即拒绝运动。
ChassisStopReason ChassisControl_SafetyReason(const ChassisControl* control, uint32_t now_ms);
// 查询运行期故障、通信与反馈时效门禁。依次检查锁存、节点在线、Bus-Off、驱动故障、OTA 与反馈超时；标定和角度门禁由完整检查函数补充。
ChassisStopReason ChassisControl_RuntimeSafetyReason(const ChassisControl* control, uint32_t now_ms);
// 复制最新一组底盘反馈到控制上下文。调用方须提供一致的双轮、双角度和时间戳快照；本函数仅复制，不推断反馈是否新鲜。
void ChassisControl_SetFeedback(ChassisControl* control, const ChassisFeedback* feedback);
// 仲裁控制会话并提交经过校验的行走与转向目标。支持 acquire、renew、stop；校验所有者、序号、租约和幅度。反向请求先令目标为零并等待反馈确认；stop 可撤目标但仅按会话归属释放控制权。
uint8_t ChassisControl_Apply(ChassisControl* control, const char* action, const char* session_id,
                             uint32_t seq, uint32_t lease_ms, int16_t drive_rpm,
                             int32_t steering_mdeg, uint32_t now_ms);
// 周期检查租约、安全门禁和换向停车确认。等待换向时保持零速，每个新反馈最多计一次；双轮不超过 20 rpm 连续三次才放行，6 秒期限后请求故障撤使能与锁存。
void ChassisControl_Tick(ChassisControl* control, uint32_t now_ms);
// 查询当前全部正常运动门禁是否通过。复用统一安全原因检查；通过仅表示软件条件允许，不代表驱动器已经执行目标或车辆已经运动。
uint8_t ChassisControl_SafetyReady(const ChassisControl* control, uint32_t now_ms);

#endif
