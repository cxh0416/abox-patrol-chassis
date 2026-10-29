#include "patrol_phy_remote.h"

#include <stdlib.h>
#include <string.h>

#include "center_calibration.h"
#include "remote_service.h"
#include "vehicle_power_output.h"

/* >>>>> PHY-REMOTE: START >>>>> */
#define PATROL_PHY_REMOTE_SESSION         "phy-remote"
#define PATROL_PHY_REMOTE_FULL_SCALE      450
#define PATROL_PHY_REMOTE_MAX_ANGLE_MDEG  25000  /* +/-25 deg, matches keyboard default */
#define PATROL_PHY_REMOTE_MAX_RPM         CHASSIS_MAX_RPM
/* Handset inactive -> stop now, power off only after the bounded stop sequence
 * has time to finish. Immediate power-off while an executor is still stopping
 * would trip an execution fault. */
#define PATROL_PHY_REMOTE_POWER_OFF_GRACE_MS 3000U
#define PATROL_PHY_REMOTE_STEER_RATE_MDEG_S 12000
#define PATROL_PHY_REMOTE_STEER_REVERSAL_MDEG 5000
#define PATROL_PHY_REMOTE_STEER_CENTER_MDEG 1000
#define PATROL_PHY_REMOTE_STEER_CENTER_SAMPLES 3U

static uint32_t control_seq;
static uint32_t power_off_deadline_ms;   /* 0 = no pending power-off */
static uint32_t last_switch_ms;
static uint16_t steering_command_ms;
static struct {
    uint16_t armed : 1;
    uint16_t last_enable : 1;
    uint16_t powered_by_remote : 1;
    uint16_t switch_seen : 1;
    uint16_t steering_center_mode : 1;
    uint16_t steering_center_samples : 2;
} g_phy;
#define armed g_phy.armed
#define last_enable g_phy.last_enable
#define powered_by_remote g_phy.powered_by_remote
#define switch_seen g_phy.switch_seen
#define steering_center_mode g_phy.steering_center_mode
#define steering_center_samples g_phy.steering_center_samples

// 检查实体 T12 是否持有当前共享控制会话。必须同时存在活动会话且 session_id 匹配 phy-remote，避免停止或续租其他控制方的会话。
static uint8_t owns(void)
{
    const ChassisControl* c = RemoteService_State();
    return c->session_active && !strcmp(c->session_id, PATROL_PHY_REMOTE_SESSION);
}

// 按固定时间内最大角度变化量推进转向目标；采用64位中间量，时钟停顿或异常间隔不得制造大跨度目标。
static int32_t slew_steering(int32_t current, int32_t requested, uint32_t now_ms)
{
    uint16_t elapsed = steering_command_ms == 0U ? 10U :
        (uint16_t)((uint16_t)now_ms - steering_command_ms);
    int64_t max_step;
    int64_t delta;
    if (elapsed < 10U) elapsed = 10U;
    if (elapsed > 100U) elapsed = 100U;
    max_step = (int64_t)PATROL_PHY_REMOTE_STEER_RATE_MDEG_S * elapsed / 1000;
    delta = (int64_t)requested - current;
    if (delta > max_step) current = (int32_t)((int64_t)current + max_step);
    else if (delta < -max_step) current = (int32_t)((int64_t)current - max_step);
    else current = requested;
    steering_command_ms = (uint16_t)now_ms;
    return current;
}

// 大幅跨中位反向时先回到机械中位；实时中心判断只使用电机位置，连续稳定后恢复向新目标移动。
static void update_center_transition(const ChassisFeedback* feedback, int32_t commanded_mdeg)
{
    if (!steering_center_mode || feedback == 0) return;
    if (commanded_mdeg < -PATROL_PHY_REMOTE_STEER_CENTER_MDEG ||
        commanded_mdeg > PATROL_PHY_REMOTE_STEER_CENTER_MDEG ||
        llabs((int64_t)feedback->steering_actual_mdeg) > PATROL_PHY_REMOTE_STEER_CENTER_MDEG) {
        steering_center_samples = 0U;
        return;
    }
    if (steering_center_samples < PATROL_PHY_REMOTE_STEER_CENTER_SAMPLES)
        steering_center_samples++;
    if (steering_center_samples >= PATROL_PHY_REMOTE_STEER_CENTER_SAMPLES)
        steering_center_mode = 0U;
}

// 撤销实体遥控目标并释放其本地武装状态。通过统一 stop 仲裁清除目标，后续执行器负责有序停机；本函数不直接操作驱动器寄存器。
static void stop_remote(uint32_t now_ms)
{
    /* "stop" always takes effect and releases our session; targets go to zero
     * and the shared executor performs its bounded stop sequence. */
    (void)RemoteService_ApplyLocal("stop", PATROL_PHY_REMOTE_SESSION, 0, 0, 0, 0, now_ms);
    armed = 0U;
    steering_command_ms = 0U;
    steering_center_mode = 0U;
    steering_center_samples = 0U;
}

// 将 T12 有符号摇杆量换算为转向目标。按满量程 450 映射到正负 25000 mdeg，并钳位异常超幅输入。
static int32_t map_steering(int16_t raw)
{
    int32_t value = ((int32_t)raw * PATROL_PHY_REMOTE_MAX_ANGLE_MDEG) /
                    PATROL_PHY_REMOTE_FULL_SCALE;
    if (value > PATROL_PHY_REMOTE_MAX_ANGLE_MDEG) value = PATROL_PHY_REMOTE_MAX_ANGLE_MDEG;
    if (value < -PATROL_PHY_REMOTE_MAX_ANGLE_MDEG) value = -PATROL_PHY_REMOTE_MAX_ANGLE_MDEG;
    return value;
}

// 将 T12 油门摇杆量换算为车体目标转速。按满量程 450 映射到 CHASSIS_MAX_RPM，输出钳位于当前正负 2000 rpm 上限。
static int16_t map_rpm(int16_t raw)
{
    int32_t value = ((int32_t)raw * PATROL_PHY_REMOTE_MAX_RPM) /
                    PATROL_PHY_REMOTE_FULL_SCALE;
    if (value > PATROL_PHY_REMOTE_MAX_RPM) value = PATROL_PHY_REMOTE_MAX_RPM;
    if (value < -PATROL_PHY_REMOTE_MAX_RPM) value = -PATROL_PHY_REMOTE_MAX_RPM;
    return (int16_t)value;
}

// 重置 T12 接收、会话武装和延迟下电状态。初始 A 视为关闭，首次有效开启帧形成上升沿；不自动建立运动目标。
void PatrolPhyRemote_Init(void)
{
    control_seq = 0U;
    armed = 0U;
    power_off_deadline_ms = 0U;
    last_enable = 0U;   /* 0 = inactive; first nonzero frame is a rising edge */
    powered_by_remote = 0U;
    switch_seen = 0U;
    last_switch_ms = 0U;
    steering_command_ms = 0U;
    steering_center_mode = 0U;
    steering_center_samples = 0U;
}

// 处理 T12 开关与摇杆帧并提交共享控制请求。校验 8 字节长度、100 ms 接收时效及开关帧顺序；A 控制使能和延迟下电，B 字节不执行动作，摇杆必须有新鲜开关流和有效会话。
uint8_t PatrolPhyRemote_OnFrame(const PatrolCanFrame* frame, uint32_t received_ms, uint32_t now_ms)
{
    if (frame == 0) return 0U;

    if (frame->id != PATROL_PHY_REMOTE_SWITCH_ID && frame->id != PATROL_PHY_REMOTE_JOYSTICK_ID)
        return 0U;
    /* Reject queued old switch frames before updating enable state. */
    if (frame->dlc != 8U || (uint32_t)(now_ms - received_ms) > 100U)
        return 1U;

    if (frame->id == PATROL_PHY_REMOTE_SWITCH_ID) {
        if (frame->dlc == 8U) {
            if (switch_seen && (int32_t)(received_ms - last_switch_ms) < 0) return 1U;
            switch_seen = 1U;
            last_switch_ms = received_ms;
            /* B (data[3]) is reserved and has no control action. */
            /* byte2 = channel A = handset enable (nonzero = active, 0 = inactive). */
            uint8_t enable = frame->data[2];
            uint8_t was_active = (last_enable != 0U);
            uint8_t active = (enable != 0U);
            last_enable = enable != 0U;
            if (active) {
                if (!was_active) {
                    /* Rising edge: cancel any pending power-off and power on.
                     * Claim power ownership only when this module switched the
                     * output on, so it never steals a web/keyboard power-off. */
                    power_off_deadline_ms = 0U;
                    if (!VehiclePowerOutput_IsOn() && VehiclePowerOutput_Set(1U))
                        powered_by_remote = 1U;
                }
                /* Acquire the shared session; retry each switch frame until it
                 * succeeds. Targets stay zero until a joystick frame renews. */
                if (!armed &&
                    RemoteService_ApplyLocal("acquire", PATROL_PHY_REMOTE_SESSION,
                                             ++control_seq, PATROL_PHY_REMOTE_LEASE_MS,
                                             0, 0, now_ms)) {
                    armed = 1U;
                    steering_command_ms = (uint16_t)now_ms;
                    steering_center_mode = 0U;
                    steering_center_samples = 0U;
                }
            } else if (was_active) {
                /* Falling edge: fail-closed stop, then power off after the
                 * bounded stop sequence finishes. */
                if (armed || owns())
                    stop_remote(now_ms);
                if (powered_by_remote)
                    power_off_deadline_ms = now_ms + PATROL_PHY_REMOTE_POWER_OFF_GRACE_MS;
            }
        }
        return 1U;
    }

    if (frame->id == PATROL_PHY_REMOTE_JOYSTICK_ID) {
        if (frame->dlc == 8U && armed) {
            if (!switch_seen || (uint32_t)(now_ms - last_switch_ms) >= PATROL_PHY_REMOTE_LEASE_MS) {
                stop_remote(now_ms);
                return 1U;
            }
            int16_t raw_steer = (int16_t)((uint16_t)frame->data[0] |
                                         ((uint16_t)frame->data[1] << 8));
            int16_t raw_throttle = (int16_t)((uint16_t)frame->data[4] |
                                            ((uint16_t)frame->data[5] << 8));
            const ChassisControl* state = RemoteService_State();
            int32_t requested = map_steering(raw_steer);
            int32_t ordered = state->target_steering_mdeg;
            update_center_transition(&state->feedback, ordered);
            if (!steering_center_mode &&
                ((ordered > 0 && requested < 0) || (ordered < 0 && requested > 0)) &&
                llabs((int64_t)requested - ordered) >= PATROL_PHY_REMOTE_STEER_REVERSAL_MDEG) {
                steering_center_mode = 1U;
                steering_center_samples = 0U;
            }
            ordered = slew_steering(ordered, steering_center_mode ? 0 : requested, now_ms);
            if (!RemoteService_ApplyLocal("renew", PATROL_PHY_REMOTE_SESSION,
                                          ++control_seq, PATROL_PHY_REMOTE_LEASE_MS,
                                          map_rpm(raw_throttle), ordered,
                                          now_ms)) {
                stop_remote(now_ms);
            }
        }
        return 1U;
    }

    return 0U;
}

// 周期检查 T12 租约、开关流和安全条件。通信不健康、标定占用、会话丢失或门禁失败即撤目标；A 关闭后的下电请求按既有停车宽限时间处理。
void PatrolPhyRemote_Tick(uint32_t now_ms, uint8_t transport_healthy)
{
    const ChassisControl* c;
    ChassisStopReason safety_reason;
    if (power_off_deadline_ms != 0U &&
        (int32_t)(now_ms - power_off_deadline_ms) >= 0) {
        (void)VehiclePowerOutput_Set(0U);
        power_off_deadline_ms = 0U;
        powered_by_remote = 0U;
    }
    if (!armed) return;
    c = RemoteService_State();
    if (!transport_healthy || !switch_seen ||
        (uint32_t)(now_ms - last_switch_ms) >= PATROL_PHY_REMOTE_LEASE_MS ||
        CenterCalibration_IsActive() || !owns() ||
        !c->enable_request || (int32_t)(now_ms - c->lease_deadline_ms) >= 0) {
        stop_remote(now_ms);
        return;
    }
    safety_reason = ChassisControl_SafetyReason(c, now_ms);
    if (safety_reason != CHASSIS_STOP_NONE) stop_remote(now_ms);
}
/* <<<<< PHY-REMOTE: END <<<<< */
