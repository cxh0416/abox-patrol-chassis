#include "chassis_control.h"
#include "vehicle_steering_profile.h"

#include <stdlib.h>
#include <string.h>

// 提取车体目标转速的方向符号。只区分前进、后退和零速，不涉及左右轮镜像安装方向；返回 1、-1 或 0。
static int8_t direction_of(int16_t rpm) { return (rpm > 0) ? 1 : ((rpm < 0) ? -1 : 0); }
// 判断左右驱动轮是否都已进入停车确认范围。两轮实测转速绝对值均不超过 20 rpm 才成立；本函数不计数，连续三次确认由调用方负责。
static uint8_t stopped(const ChassisFeedback* feedback)
{
    return (abs(feedback->left_actual_rpm) <= 20 && abs(feedback->right_actual_rpm) <= 20) ? 1U : 0U;
}

// 统一撤销运动目标并按原因更新停车状态。清除行走、转向、待换向请求和使能；保留已锁存的首个故障原因，按 release 决定是否释放会话。
static void force_stop(ChassisControl* control, ChassisStopReason reason, uint8_t release)
{
    if (reason == CHASSIS_STOP_DIRECTION_TIMEOUT || reason == CHASSIS_STOP_ENCODER_RANGE ||
        reason == CHASSIS_STOP_ENCODER_PROFILE) ChassisControl_RequestFault(control, reason);
    control->target_rpm = 0;
    control->target_steering_mdeg = 0;
    control->requested_rpm = 0;
    control->reversal_pending = 0U;
    control->zero_confirmations = 0U;
    control->enable_request = 0U;
    if (!control->fault_latched && !control->fault_pending) control->stop_reason = reason;
    if (release) {
        control->session_active = 0U;
        control->session_id[0] = '\0';
    }
}

// 初始化底盘控制上下文到未标定的安全默认状态。清空会话、目标、反馈和换向计数；初始化本身不会使能电机，后续仍须通过反馈安全门禁。
void ChassisControl_Init(ChassisControl* control)
{
    memset(control, 0, sizeof(*control));
    control->stop_reason = CHASSIS_STOP_UNCALIBRATED;
}

// 把停机传输交给产品运行层，控制核心不直接访问 HAL、CAN 邮箱或驱动器参数。
void ChassisControl_SetFaultStopHandler(ChassisControl* control, uint8_t (*handler)(void))
{
    control->request_fault_stop = handler;
}

// 在清零目标之前让运行层冻结首故障；pending 与 latched 一样禁止运动，避免等待撤使能时被遥控重新接管。
void ChassisControl_RequestFault(ChassisControl* control, ChassisStopReason reason)
{
    if (!control->fault_latched && !control->fault_pending) {
        control->stop_reason = reason;
        control->fault_pending = 1U;
        if (!control->request_fault_stop || !control->request_fault_stop())
            ChassisControl_CompleteFaultStop(control);
    }
    control->target_rpm = control->requested_rpm = 0;
    control->target_steering_mdeg = 0;
    control->reversal_pending = control->zero_confirmations = 0U;
    control->enable_request = control->session_active = 0U;
    control->session_id[0] = '\0';
    control->auto_center_inhibited = 1U;
}

// 只把待锁存变成已锁存，不清除首因；确认失败时同样锁存，保证通信故障不会使安全流程无限等待。
void ChassisControl_CompleteFaultStop(ChassisControl* control)
{
    if (!control->fault_pending) return;
    control->fault_pending = 0U;
    control->fault_latched = 1U;
}

// 设置内部行走禁用状态，不撤销转向授权。激活时立即清零行走与待换向目标，保留最近非零方向以防解除后绕过换向检查；当前不绑定 T12 B 键。
void ChassisControl_SetDriveBrake(ChassisControl* control, uint8_t active)
{
    control->drive_brake = active != 0U;
    if (control->drive_brake) {
        control->target_rpm = 0;
        control->requested_rpm = 0;
        control->reversal_pending = 0U;
        control->zero_confirmations = 0U;
        /* Keep the last nonzero direction: release must not bypass reversal. */
    }
}

// 复制最新一组底盘反馈到控制上下文。调用方须提供一致的双轮、双角度和时间戳快照；本函数仅复制，不推断反馈是否新鲜。
void ChassisControl_SetFeedback(ChassisControl* control, const ChassisFeedback* feedback)
{
    control->feedback = *feedback;
}

// 检查驱动器与绝对编码器角度是否均处于硬保护范围。采用 64 位绝对值运算处理符号边界；硬范围由转向配置常量决定，不等同于普通目标软限位。
static uint8_t steering_hard_range(const ChassisFeedback* f)
{
    return llabs((int64_t)f->steering_actual_mdeg)<=VEHICLE_STEERING_TRIP_MDEG &&
           llabs((int64_t)f->encoder_actual_mdeg)<=VEHICLE_STEERING_TRIP_MDEG;
}
// 更新转向边界保持状态并限制越界方向目标。触边时禁止行走、取消换向并将转向目标限制到恢复角度；仅在允许清除且请求与双反馈均回到恢复范围时解除。
static void steering_boundary(ChassisControl* c, int32_t requested, uint8_t allow_clear)
{
    const ChassisFeedback* f=&c->feedback;
    if (f->steering_actual_mdeg>CHASSIS_MAX_STEERING_MDEG || f->encoder_actual_mdeg>CHASSIS_MAX_STEERING_MDEG)
        c->steering_boundary=1;
    else if (f->steering_actual_mdeg<-CHASSIS_MAX_STEERING_MDEG || f->encoder_actual_mdeg<-CHASSIS_MAX_STEERING_MDEG)
        c->steering_boundary=-1;
    else if (allow_clear && llabs((int64_t)requested)<=VEHICLE_STEERING_RECOVER_MDEG &&
             llabs((int64_t)f->steering_actual_mdeg)<=VEHICLE_STEERING_RECOVER_MDEG &&
             llabs((int64_t)f->encoder_actual_mdeg)<=VEHICLE_STEERING_RECOVER_MDEG)
        c->steering_boundary=0;
    if (!c->steering_boundary) return;
    c->target_rpm=0; c->requested_rpm=0; c->reversal_pending=0; c->zero_confirmations=0;
    if (c->steering_boundary>0 && c->target_steering_mdeg>VEHICLE_STEERING_RECOVER_MDEG)
        c->target_steering_mdeg=VEHICLE_STEERING_RECOVER_MDEG;
    if (c->steering_boundary<0 && c->target_steering_mdeg<-VEHICLE_STEERING_RECOVER_MDEG)
        c->target_steering_mdeg=-VEHICLE_STEERING_RECOVER_MDEG;
}

// 查询当前全部正常运动门禁是否通过。复用统一安全原因检查；通过仅表示软件条件允许，不代表驱动器已经执行目标或车辆已经运动。
uint8_t ChassisControl_SafetyReady(const ChassisControl* control, uint32_t now_ms)
{
    return ChassisControl_SafetyReason(control, now_ms) == CHASSIS_STOP_NONE;
}

// 查询运行期故障、通信与反馈时效门禁。依次检查锁存、节点在线、Bus-Off、驱动故障、OTA 与反馈超时；标定和角度门禁由完整检查函数补充。
ChassisStopReason ChassisControl_RuntimeSafetyReason(const ChassisControl* control, uint32_t now_ms)
{
    const ChassisFeedback* f = &control->feedback;
    if (control->fault_latched || control->fault_pending) return control->stop_reason;
    if (!f->steering_online || !f->drive_online) return CHASSIS_STOP_NODE_OFFLINE;
    if (f->can_bus_off) return CHASSIS_STOP_CAN_BUS_OFF;
    if (f->drive_fault) return CHASSIS_STOP_DRIVE_FAULT;
    if (f->ota_busy) return CHASSIS_STOP_OTA_BUSY;
    if ((now_ms - f->feedback_ms) > CHASSIS_FEEDBACK_TIMEOUT_MS) return CHASSIS_STOP_FEEDBACK_STALE;
    return CHASSIS_STOP_NONE;
}

// 返回阻止正常运动的首个安全原因。在运行期门禁基础上检查标定、编码器规格、量程、电源就绪和双角度偏差；偏差超过 2000 mdeg 即拒绝运动。
ChassisStopReason ChassisControl_SafetyReason(const ChassisControl* control, uint32_t now_ms)
{
    const ChassisFeedback* f = &control->feedback;
    ChassisStopReason reason = ChassisControl_RuntimeSafetyReason(control, now_ms);
    if (reason != CHASSIS_STOP_NONE) return reason;
    if (!f->calibrated) return CHASSIS_STOP_UNCALIBRATED;
    if (!f->encoder_profile_ready) return CHASSIS_STOP_ENCODER_PROFILE;
    if (!f->encoder_range_valid || !steering_hard_range(f)) return CHASSIS_STOP_ENCODER_RANGE;
    if (!f->power_ready) return CHASSIS_STOP_POWER_NOT_READY;
    if (llabs((int64_t)f->steering_actual_mdeg - f->encoder_actual_mdeg) > 2000L) return CHASSIS_STOP_ANGLE_MISMATCH;
    return CHASSIS_STOP_NONE;
}

// 仲裁控制会话并提交经过校验的行走与转向目标。支持 acquire、renew、stop；校验所有者、序号、租约和幅度。反向请求先令目标为零并等待反馈确认；stop 可撤目标但仅按会话归属释放控制权。
uint8_t ChassisControl_Apply(ChassisControl* control, const char* action, const char* session_id,
                             uint32_t seq, uint32_t lease_ms, int16_t drive_rpm,
                             int32_t steering_mdeg, uint32_t now_ms)
{
    int8_t requested_direction;
    if (action == 0 || session_id == 0 || session_id[0] == '\0') return 0U;
    if (strcmp(action, "stop") == 0) {
        uint8_t release = (uint8_t)(!control->session_active ||
            strcmp(control->session_id, session_id) == 0);
        force_stop(control, CHASSIS_STOP_EXPLICIT, release);
        control->auto_center_inhibited = 1U;
        return 1U;
    }
    if (strcmp(action, "acquire") != 0 && strcmp(action, "renew") != 0) return 0U;
    if (control->fault_latched || control->fault_pending) return 0U;
    if (lease_ms == 0U || lease_ms > CHASSIS_MAX_LEASE_MS || abs(drive_rpm) > CHASSIS_MAX_RPM ||
        labs(steering_mdeg) > CHASSIS_MAX_STEERING_MDEG) return 0U;
    if (strcmp(action, "acquire") == 0) {
        if (control->session_active && strcmp(control->session_id, session_id) != 0) return 0U;
        strncpy(control->session_id, session_id, sizeof(control->session_id) - 1U);
        control->session_id[sizeof(control->session_id) - 1U] = '\0';
        control->session_active = 1U;
        control->last_seq = 0U;
    } else if (!control->session_active || strcmp(control->session_id, session_id) != 0) return 0U;
    if (seq <= control->last_seq || ChassisControl_SafetyReason(control, now_ms) != CHASSIS_STOP_NONE) {
        force_stop(control, ChassisControl_SafetyReason(control, now_ms), 0U);
        return 0U;
    }
    control->last_seq = seq;
    control->lease_deadline_ms = now_ms + lease_ms;
    control->target_steering_mdeg = steering_mdeg;
    if (control->drive_brake) drive_rpm = 0;
    requested_direction = direction_of(drive_rpm);
    if (requested_direction != 0 && control->active_direction != 0 && requested_direction != control->active_direction) {
        control->requested_rpm = drive_rpm;
        control->target_rpm = 0;
        if (!control->reversal_pending) {
            control->reversal_pending = 1U;
            control->zero_confirmations = 0U;
            control->reversal_started_ms = now_ms;
            control->last_zero_feedback_ms = control->feedback.feedback_ms;
        }
    } else {
        control->requested_rpm = drive_rpm;
        control->target_rpm = drive_rpm;
        /* Releasing/changing a pending reverse command cancels it. Retain the
         * last nonzero direction so a later reversal still confirms zero. */
        control->reversal_pending = 0U;
        control->zero_confirmations = 0U;
        if (requested_direction != 0) control->active_direction = requested_direction;
    }
    steering_boundary(control, steering_mdeg, 1U);
    control->enable_request = 1U;
    control->auto_center_inhibited = 0U;
    control->stop_reason = CHASSIS_STOP_NONE;
    return 1U;
}

// 周期检查租约、安全门禁和换向停车确认。等待换向时保持零速，每个不同反馈时间戳最多计一次，两轮均不超过 20 rpm 连续三次才放行；超过当前代码的 2 秒期限撤使能并锁存故障。
void ChassisControl_Tick(ChassisControl* control, uint32_t now_ms)
{
    ChassisStopReason reason = ChassisControl_SafetyReason(control, now_ms);
    if (reason != CHASSIS_STOP_NONE) { force_stop(control, reason, 0U); return; }
    if (control->session_active && (int32_t)(now_ms - control->lease_deadline_ms) >= 0) {
        force_stop(control, CHASSIS_STOP_LEASE_EXPIRED, 1U);
        return;
    }
    steering_boundary(control, control->target_steering_mdeg, 0U);
    if (control->drive_brake) {
        ChassisControl_SetDriveBrake(control, 1U);
        return;
    }
    if (!control->reversal_pending) return;
    control->target_rpm = 0;
    if (control->feedback.feedback_ms != control->last_zero_feedback_ms) {
        control->last_zero_feedback_ms = control->feedback.feedback_ms;
        if (stopped(&control->feedback)) {
            if (++control->zero_confirmations >= 3U) {
                control->target_rpm = control->requested_rpm;
                control->active_direction = direction_of(control->requested_rpm);
                control->reversal_pending = 0U;
                control->zero_confirmations = 0U;
            }
        } else control->zero_confirmations = 0U;
    }
    if (control->reversal_pending && (now_ms - control->reversal_started_ms) >= 6000U)
        force_stop(control, CHASSIS_STOP_DIRECTION_TIMEOUT, 1U);
}
