#include "patrol_phy_remote.h"

#include <string.h>

#include "center_calibration.h"
#include "remote_service.h"
#include "vehicle_power_output.h"

/* >>>>> PHY-REMOTE: START >>>>> */
#define PATROL_PHY_REMOTE_SESSION         "phy-remote"
#define PATROL_PHY_REMOTE_FULL_SCALE      450
#define PATROL_PHY_REMOTE_MAX_ANGLE_MDEG  25000  /* +/-25 deg, matches keyboard default */
#define PATROL_PHY_REMOTE_MAX_RPM         500    /* CHASSIS_MAX_RPM */
/* Handset inactive -> stop now, power off only after the bounded stop sequence
 * has time to finish. Immediate power-off while an executor is still stopping
 * would trip an execution fault. */
#define PATROL_PHY_REMOTE_POWER_OFF_GRACE_MS 3000U

static uint32_t control_seq;
static uint8_t armed;
static uint32_t power_off_deadline_ms;   /* 0 = no pending power-off */
static uint8_t last_enable;              /* previous switch-frame enable; nonzero = active */
static uint8_t powered_by_remote;        /* this module commanded power-on */
static uint8_t switch_seen;
static uint32_t last_switch_ms;

static uint8_t owns(void)
{
    const ChassisControl* c = RemoteService_State();
    return c->session_active && !strcmp(c->session_id, PATROL_PHY_REMOTE_SESSION);
}

static void stop_remote(uint32_t now_ms)
{
    /* "stop" always takes effect and releases our session; targets go to zero
     * and the shared executor performs its bounded stop sequence. */
    (void)RemoteService_ApplyLocal("stop", PATROL_PHY_REMOTE_SESSION, 0, 0, 0, 0, now_ms);
    armed = 0U;
}

static int32_t map_steering(int16_t raw)
{
    int32_t value = ((int32_t)raw * PATROL_PHY_REMOTE_MAX_ANGLE_MDEG) /
                    PATROL_PHY_REMOTE_FULL_SCALE;
    if (value > PATROL_PHY_REMOTE_MAX_ANGLE_MDEG) value = PATROL_PHY_REMOTE_MAX_ANGLE_MDEG;
    if (value < -PATROL_PHY_REMOTE_MAX_ANGLE_MDEG) value = -PATROL_PHY_REMOTE_MAX_ANGLE_MDEG;
    return value;
}

static int16_t map_rpm(int16_t raw)
{
    int32_t value = ((int32_t)raw * PATROL_PHY_REMOTE_MAX_RPM) /
                    PATROL_PHY_REMOTE_FULL_SCALE;
    if (value > PATROL_PHY_REMOTE_MAX_RPM) value = PATROL_PHY_REMOTE_MAX_RPM;
    if (value < -PATROL_PHY_REMOTE_MAX_RPM) value = -PATROL_PHY_REMOTE_MAX_RPM;
    return (int16_t)value;
}

void PatrolPhyRemote_Init(void)
{
    control_seq = 0U;
    armed = 0U;
    power_off_deadline_ms = 0U;
    last_enable = 0U;   /* 0 = inactive; first nonzero frame is a rising edge */
    powered_by_remote = 0U;
    switch_seen = 0U;
    last_switch_ms = 0U;
}

uint8_t PatrolPhyRemote_OnFrame(const PatrolCanFrame* frame, uint32_t received_ms, uint32_t now_ms)
{
    if (frame == 0) return 0U;

    if (frame->id != PATROL_PHY_REMOTE_SWITCH_ID && frame->id != PATROL_PHY_REMOTE_JOYSTICK_ID)
        return 0U;
    /* A queued old switch must never release a newer brake state. */
    if (frame->dlc != 8U || (uint32_t)(now_ms - received_ms) > 100U)
        return 1U;

    if (frame->id == PATROL_PHY_REMOTE_SWITCH_ID) {
        if (frame->dlc == 8U) {
            if (switch_seen && (int32_t)(received_ms - last_switch_ms) < 0) return 1U;
            switch_seen = 1U;
            last_switch_ms = received_ms;
            /* Operator confirmed B follows A: lamp on/nonzero, lamp off/zero.
             * Use the transmitted state, never toggle on each cyclic frame. */
            RemoteService_SetDriveBrake(frame->data[3] != 0U);
            /* byte2 = channel A = handset enable (nonzero = active, 0 = inactive). */
            uint8_t enable = frame->data[2];
            uint8_t was_active = (last_enable != 0U);
            uint8_t active = (enable != 0U);
            last_enable = enable;
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
            if (!RemoteService_ApplyLocal("renew", PATROL_PHY_REMOTE_SESSION,
                                          ++control_seq, PATROL_PHY_REMOTE_LEASE_MS,
                                          map_rpm(raw_throttle), map_steering(raw_steer),
                                          now_ms)) {
                stop_remote(now_ms);
            }
        }
        return 1U;
    }

    return 0U;
}

void PatrolPhyRemote_Tick(uint32_t now_ms, uint8_t transport_healthy)
{
    const ChassisControl* c;
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
        !c->enable_request || (int32_t)(now_ms - c->lease_deadline_ms) >= 0 ||
        !ChassisControl_SafetyReady(c, now_ms)) {
        stop_remote(now_ms);
    }
}
/* <<<<< PHY-REMOTE: END <<<<< */
