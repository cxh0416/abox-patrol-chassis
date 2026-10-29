#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "patrol_can_remote.h"
#include "patrol_phy_remote.h"
#include "remote_service.h"
#include "center_calibration.h"
#include "patrol_config.h"
#include "vehicle_power_output.h"
#include "time_base.h"
#include "chassis_cloud_protocol.h"
#include "abox_mqtt_v4.h"
#include "abox_mqtt_digest.h"
#include "fixed_point.h"

/* These dependencies must never be consulted by the offline CAN path. */
#define UNUSED __attribute__((unused))
ABoxClockSnapshot TimeBase_GetSnapshot(void)
{ assert(!"CAN2 must not require network time"); ABoxClockSnapshot s={0}; return s; }
uint8_t TimeBase_FormatUtcMs(char* p UNUSED,uint16_t n UNUSED)
{ assert(!"CAN2 must not format MQTT responses"); return 0; }
ChassisCloudStatus ChassisCloud_Parse(const char* p UNUSED,uint16_t n UNUSED,ChassisCloudCommand* c UNUSED)
{ assert(!"CAN2 must not parse MQTT"); return CHASSIS_CLOUD_INVALID; }
uint8_t ChassisCloud_RequestDigest(const ChassisCloudCommand* c UNUSED,uint8_t d[32] UNUSED)
{ assert(!"CAN2 must not hash MQTT requests"); return 0; }
int ABoxMqttDigest_NullRequest(const char* c UNUSED,int h UNUSED,uint64_t e UNUSED,uint8_t d[32] UNUSED)
{ assert(!"CAN2 must not hash MQTT requests"); return 0; }
uint16_t ABoxMqttV4_CheckExpiry(const ABoxClockSnapshot* c UNUSED,uint64_t e UNUSED)
{ assert(!"CAN2 must not require UTC expiry"); return 0; }
uint64_t ABoxMqttV4_IdempotencyProtectUntil(uint64_t e UNUSED)
{ assert(!"CAN2 must not require network replay cache"); return 0; }

static uint32_t now;
static uint8_t center_active, ota, reset_allowed;
static uint8_t cal_phase, cal_reason;
static int32_t cal_driver_raw=-1464, cal_saved_center=-1464, cal_jog_offset;
static char cal_session[40];
static PatrolRemoteConfigV5_t config;
uint32_t HAL_GetTick(void) { return now; }
const PatrolRemoteConfigV5_t* PatrolConfig_Get(void) { return &config; }
uint8_t PatrolConfig_CalibrationCompatible(void) { return 1; }
uint8_t CenterCalibration_IsActive(void) { return center_active; }
const char* CenterCalibration_SessionId(void) { return cal_session; }
uint8_t CenterCalibration_Start(void)
{
    if (center_active) return 0U;
    center_active=1U; cal_phase=9U; cal_reason=0U;
    strcpy(cal_session,"can2-local-test");
    return 1U;
}
uint8_t CenterCalibration_GetStatus(CenterCalibrationStatus* status)
{
    if (!status) return 0U;
    memset(status,0,sizeof(*status));
    status->phase=cal_phase; status->reason=cal_reason; status->active=center_active;
    status->calibrated=config.calibrated; status->driver_raw=cal_driver_raw;
    status->encoder_raw=0U; status->jog_offset_mdeg=cal_jog_offset;
    status->saved_center_count=cal_saved_center;
    return 1U;
}
uint8_t CenterCalibration_KeepAlive(const char* id)
{ return center_active && id && !strcmp(id,cal_session); }
uint8_t CenterCalibration_Jog(const char* id, int16_t delta)
{
    if (!center_active || !id || strcmp(id,cal_session)) return 0U;
    cal_phase=1U; cal_jog_offset+=delta; return 1U;
}
uint8_t CenterCalibration_Commit(const char* id)
{
    if (!center_active || !id || strcmp(id,cal_session)) return 0U;
    cal_phase=4U; return 1U;
}
uint8_t CenterCalibration_Cancel(const char* id)
{
    if (!center_active || !id || strcmp(id,cal_session)) return 0U;
    center_active=0U; cal_phase=0U; return 1U;
}
uint8_t PatrolRuntimePort_OtaBusy(void) { return ota; }
/* This protocol-only fixture compiles no motor transport. */
uint8_t PatrolRuntime_RequestFaultStop(void) { return 0U; }
uint8_t PatrolRuntime_ResetFault(void)
{
    if (VehiclePowerOutput_IsOn() || ota || !reset_allowed) return 0;
    RemoteService_Init(); return 1;
}
static uint8_t write_power(uint8_t on) { (void)on; return 1; }
static void healthy(void)
{
    ChassisFeedback f={0};
    f.calibrated=f.steering_online=f.drive_online=f.power_ready=1;
    f.encoder_range_valid=f.encoder_profile_ready=1; f.feedback_ms=now;
    f.angle_pair_valid=1U;
    RemoteService_UpdateFeedback(&f);
}
static void init(void)
{
    now=1000; center_active=ota=0; reset_allowed=1;
    memset(&config,0,sizeof(config)); config.calibrated=1;
    VehiclePowerOutput_Init(write_power); assert(VehiclePowerOutput_Set(1));
    RemoteService_SetDriveBrake(0);
    RemoteService_Init(); PatrolCanRemote_Init(); healthy();
    PatrolPhyRemote_Init();
}
static uint8_t command(uint8_t op,uint16_t seq,int16_t rpm,int16_t angle,uint32_t received)
{
    PatrolCanFrame f={PATROL_CAN_REMOTE_COMMAND,8,{1,0,0,0,0,0,0,0}},r[3];
    f.data[1]=op; f.data[2]=(uint8_t)seq; f.data[3]=(uint8_t)(seq>>8);
    f.data[4]=(uint8_t)rpm; f.data[5]=(uint8_t)((uint16_t)rpm>>8);
    f.data[6]=(uint8_t)angle; f.data[7]=(uint8_t)((uint16_t)angle>>8);
    PatrolCanRemote_OnFrame(&f,received,now);
    assert(PatrolCanRemote_TakeReply(r,now));
    assert(r[0].id==0x521 && r[1].id==0x522 && r[2].id==0x523);
    assert(r[0].data[2]==(uint8_t)seq && r[0].data[3]==(uint8_t)(seq>>8));
    assert(r[1].data[0]==(uint8_t)seq && r[2].data[0]==(uint8_t)seq);
    return r[0].data[1];
}
static uint8_t send(uint8_t op,uint16_t seq,int16_t rpm,int16_t angle)
{ return command(op,seq,rpm,angle,now); }

typedef struct {
    uint8_t result, phase, reason, flags;
    uint16_t token, encoder;
    int32_t driver, saved;
} CalibrationReply;

static CalibrationReply calibration_command(uint8_t op,uint16_t seq,uint16_t token,
                                            int16_t argument,uint32_t received)
{
    PatrolCanFrame f={PATROL_CAN_CAL_COMMAND,8,
        {PATROL_CAN_CAL_VERSION,0,0,0,0,0,0,0}},r[3];
    f.data[1]=op;
    f.data[2]=(uint8_t)seq; f.data[3]=(uint8_t)(seq>>8);
    f.data[4]=(uint8_t)token; f.data[5]=(uint8_t)(token>>8);
    f.data[6]=(uint8_t)argument; f.data[7]=(uint8_t)((uint16_t)argument>>8);
    PatrolCanRemote_OnFrame(&f,received,now);
    assert(PatrolCanRemote_TakeReply(r,now));
    assert(r[0].id==0x531 && r[1].id==0x532 && r[2].id==0x533);
    assert(r[0].data[2]==(uint8_t)seq && r[0].data[3]==(uint8_t)(seq>>8));
    assert(r[1].data[0]==(uint8_t)seq && r[2].data[0]==(uint8_t)seq);
    return (CalibrationReply){
        .result=r[0].data[1],.phase=r[0].data[4],.reason=r[0].data[5],
        .flags=r[2].data[6],
        .token=(uint16_t)(r[0].data[6] | (uint16_t)r[0].data[7]<<8),
        .encoder=(uint16_t)(r[1].data[6] | (uint16_t)r[1].data[7]<<8),
        .driver=(int32_t)((uint32_t)r[1].data[2] | (uint32_t)r[1].data[3]<<8 |
                          (uint32_t)r[1].data[4]<<16 | (uint32_t)r[1].data[5]<<24),
        .saved=(int32_t)((uint32_t)r[2].data[2] | (uint32_t)r[2].data[3]<<8 |
                         (uint32_t)r[2].data[4]<<16 | (uint32_t)r[2].data[5]<<24)
    };
}
static const ChassisControl* state(void) { return RemoteService_State(); }

static void switches(uint8_t a, uint8_t b)
{
    PatrolCanFrame f={PATROL_PHY_REMOTE_SWITCH_ID,8,{0}};
    f.data[2]=a; f.data[3]=b;
    assert(PatrolPhyRemote_OnFrame(&f,now,now));
}
static void joystick(int16_t throttle, int16_t steer)
{
    PatrolCanFrame f={PATROL_PHY_REMOTE_JOYSTICK_ID,8,{0}};
    f.data[0]=(uint8_t)steer; f.data[1]=(uint8_t)((uint16_t)steer>>8);
    f.data[4]=(uint8_t)throttle; f.data[5]=(uint8_t)((uint16_t)throttle>>8);
    assert(PatrolPhyRemote_OnFrame(&f,now,now));
}
static void joystick_until_steer(int16_t throttle, int16_t steer, int32_t expected)
{
    for (unsigned i=0; i<320U && state()->target_steering_mdeg!=expected; i++) {
        now+=10U;
        healthy();
        switches(1,0);
        joystick(throttle,steer);
    }
    assert(state()->target_steering_mdeg==expected);
}
static void physical_b_ignored_cases(void)
{
    /* Every protocol B value and repeated frames leave both targets intact. */
    for (unsigned b=0; b<=2; b++) {
        init(); switches(1,0); joystick(450,180);
        joystick_until_steer(450,180,10000);
        uint32_t deadline=state()->lease_deadline_ms;
        for (unsigned n=0; n<5; n++) {
            now+=10; healthy(); switches(1,(uint8_t)b);
            assert(!state()->drive_brake && state()->target_rpm==2000);
            assert(state()->target_steering_mdeg==10000 && state()->enable_request);
            assert(state()->session_active && state()->lease_deadline_ms==deadline);
            assert(VehiclePowerOutput_IsOn());
        }
        joystick(225,-180);
        joystick_until_steer(225,-180,-10000);
        assert(state()->target_rpm==1000 && state()->target_steering_mdeg==-10000);
        joystick(0,0); assert(state()->target_rpm==0);
    }
    /* B cannot cancel or bypass the existing three-sample reversal gate. */
    init(); switches(1,0); joystick(450,0); joystick(-450,180);
    assert(state()->reversal_pending && state()->target_rpm==0);
    for (unsigned b=0; b<=2; b++) {
        now+=10; healthy(); switches(1,(uint8_t)b);
        assert(state()->reversal_pending && state()->requested_rpm==-2000);
        RemoteService_SafetyTick(now);
        if (b<2) assert(state()->target_rpm==0);
    }
    assert(!state()->reversal_pending && state()->target_rpm==-2000);
    switches(1,1); assert(state()->target_rpm==-2000);

    /* B also has no effect on another transport's motion or enable. */
    init(); assert(RemoteService_ApplyLocal("acquire","other-owner",1,300,200,8000,now));
    switches(0,1); switches(0,2); switches(0,0);
    assert(state()->target_rpm==200 && state()->target_steering_mdeg==8000);
    assert(state()->enable_request && !strcmp(state()->session_id,"other-owner"));

    /* A off, stale switches and missing switch traffic still stop with B lit. */
    init(); VehiclePowerOutput_Set(0); switches(1,1); healthy(); joystick(450,180);
    assert(VehiclePowerOutput_IsOn() && state()->target_rpm==2000);
    PatrolCanFrame f={PATROL_PHY_REMOTE_SWITCH_ID,7,{0}};
    assert(PatrolPhyRemote_OnFrame(&f,now,now) && state()->enable_request);
    f.dlc=8;
    assert(PatrolPhyRemote_OnFrame(&f,now-101,now) && state()->enable_request);
    assert(PatrolPhyRemote_OnFrame(&f,now-1,now) && state()->enable_request);
    now+=10; switches(0,1);
    assert(!state()->enable_request && state()->target_rpm==0 && VehiclePowerOutput_IsOn());
    now+=3000; PatrolPhyRemote_Tick(now,1); assert(!VehiclePowerOutput_IsOn());
    init(); switches(1,1); joystick(450,180);
    for(unsigned n=0;n<2;n++) {
        now+=100; healthy(); joystick(450,180); assert(state()->enable_request);
    }
    now+=100; healthy(); PatrolPhyRemote_Tick(now,1);
    assert(!state()->enable_request && state()->target_rpm==0);

    /* B cannot clear a latched fault or bypass calibration requirements. */
    init(); switches(1,1); RemoteService_ExecutionFault();
    switches(1,0); joystick(450,180);
    assert(state()->fault_latched && !state()->enable_request);
    init(); switches(1,1);
    ChassisFeedback bad=state()->feedback; bad.calibrated=0; RemoteService_UpdateFeedback(&bad);
    RemoteService_SafetyTick(now); switches(1,0); joystick(450,180);
    assert(!state()->enable_request && state()->target_rpm==0);
}
static void drive_speed_cases(void)
{
    for (int sign=-1; sign<=1; sign+=2) {
        init(); switches(1,0); joystick(sign*225,sign*450);
        joystick_until_steer(sign*225,sign*450,sign*25000);
        assert(state()->target_rpm==sign*1000 && state()->target_steering_mdeg==sign*25000);
        joystick(sign*450,sign*450);
        assert(state()->target_rpm==sign*2000 && state()->target_steering_mdeg==sign*25000);
        joystick(sign*32767,sign*32767);
        assert(state()->target_rpm==sign*2000 && state()->target_steering_mdeg==sign*25000);

        init(); assert(send(PATROL_CAN_ARM,1,0,0)==PATROL_CAN_OK);
        assert(send(PATROL_CAN_TARGET,2,sign*2000,sign*30000)==PATROL_CAN_OK);
        assert(state()->target_rpm==sign*2000 && state()->target_steering_mdeg==sign*30000);
        uint32_t deadline=state()->lease_deadline_ms;
        assert(send(PATROL_CAN_TARGET,3,sign*2001,0)==PATROL_CAN_INVALID);
        assert(state()->target_rpm==sign*2000 && state()->lease_deadline_ms==deadline);
        assert(!RemoteService_ApplyLocal("renew","can2-keyboard",100,300,sign*2001,0,now));
        assert(state()->target_rpm==sign*2000);
    }
}

static void physical_steering_shaping_cases(void)
{
    init();
    healthy();
    switches(1,0);
    joystick(0,450);
    assert(state()->target_steering_mdeg > 0);
    assert(state()->target_steering_mdeg < 25000);

    for (unsigned i=0; i<220U; i++) {
        now+=10U;
        healthy();
        switches(1,0);
        joystick(0,450);
    }
    assert(state()->target_steering_mdeg == 25000);

    /* A large reversal enters the center transition and never jumps directly
     * from the positive extreme to the negative target. */
    joystick(0,-450);
    assert(state()->target_steering_mdeg > 0 && state()->target_steering_mdeg < 25000);
    for (unsigned i=0; i<215U && state()->target_steering_mdeg >= 0; i++) {
        now+=10U;
        healthy();
        switches(1,0);
        joystick(0,-450);
    }
    for (unsigned i=0; i<8U; i++) {
        now+=10U;
        healthy();
        switches(1,0);
        joystick(0,-450);
    }
    assert(state()->target_steering_mdeg < 0);
    assert(state()->target_steering_mdeg > -25000);
}

static void calibration_cases(void)
{
    CalibrationReply r;
    init();
    r=calibration_command(PATROL_CAN_CAL_QUERY,1,0,0,now);
    assert(r.result==PATROL_CAN_CAL_OK && r.token==0U && r.driver==-1464);
    assert(!PatrolCanRemote_CalibrationPrepareActive());
    assert(calibration_command(PATROL_CAN_CAL_START,2,0,0,now).result==PATROL_CAN_CAL_SAFETY);
    assert(calibration_command(PATROL_CAN_CAL_PREPARE,3,0,0,now).result==PATROL_CAN_CAL_OK);
    assert(PatrolCanRemote_CalibrationPrepareActive());
    r=calibration_command(PATROL_CAN_CAL_START,4,0,0,now);
    assert(r.result==PATROL_CAN_CAL_OK && r.token!=0U && r.phase==9U);
    assert(!PatrolCanRemote_CalibrationPrepareActive());
    uint16_t token=r.token;
    assert(calibration_command(PATROL_CAN_CAL_JOG,5,0,200,now).result==PATROL_CAN_CAL_TOKEN);
    r=calibration_command(PATROL_CAN_CAL_JOG,5,token,200,now);
    assert(r.result==PATROL_CAN_CAL_OK && r.phase==1U);
    assert(calibration_command(PATROL_CAN_CAL_JOG,5,token,-200,now).result==PATROL_CAN_CAL_SEQUENCE);
    assert(calibration_command(PATROL_CAN_CAL_COMMIT,6,token,0,now).result==PATROL_CAN_CAL_OK);
    assert(calibration_command(PATROL_CAN_CAL_QUERY,7,token,0,now).result==PATROL_CAN_CAL_OK);
    assert(calibration_command(PATROL_CAN_CAL_CANCEL,8,token,0,now).result==PATROL_CAN_CAL_OK);
    assert(!center_active);

    init(); center_active=1U;
    strcpy(cal_session,"web-session");
    PatrolCanRemote_Tick(now+PATROL_CAN_CAL_LEASE_MS,0U);
    assert(center_active);
    cal_session[0]='\0';
    assert(calibration_command(PATROL_CAN_CAL_START,10,0,0,now).result==PATROL_CAN_CAL_SAFETY);
    center_active=0U;
    assert(calibration_command(PATROL_CAN_CAL_PREPARE,11,0,0,now).result==PATROL_CAN_CAL_OK);
    r=calibration_command(PATROL_CAN_CAL_START,12,0,0,now);
    assert(r.result==PATROL_CAN_CAL_OK);
    token=r.token; now+=PATROL_CAN_CAL_LEASE_MS;
    PatrolCanRemote_Tick(now,0U);
    assert(!center_active);
}
int main(void)
{
    init(); assert(send(PATROL_CAN_QUERY,1,0,0)==PATROL_CAN_OK); assert(!state()->enable_request);
    assert(send(PATROL_CAN_TARGET,2,50,5000)==PATROL_CAN_NOT_ARMED);
    assert(send(PATROL_CAN_ARM,3,0,1)==PATROL_CAN_INVALID);
    assert(send(PATROL_CAN_ARM,10,0,0)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_TARGET,11,0,-5000)==PATROL_CAN_OK);
    assert(state()->target_steering_mdeg==-5000 && state()->target_rpm==0);
    uint32_t deadline=state()->lease_deadline_ms;
    assert(send(PATROL_CAN_TARGET,11,500,30000)==PATROL_CAN_SEQUENCE);
    assert(send(PATROL_CAN_TARGET,9,500,30000)==PATROL_CAN_SEQUENCE);
    assert(state()->lease_deadline_ms==deadline && state()->target_steering_mdeg==-5000);
    assert(command(PATROL_CAN_TARGET,12,0,0,now-101)==PATROL_CAN_STALE);
    now+=301; healthy(); RemoteService_SafetyTick(now); PatrolCanRemote_Tick(now,1);
    assert(!state()->session_active && !state()->enable_request && state()->auto_center_inhibited);
    assert(send(PATROL_CAN_TARGET,13,50,5000)==PATROL_CAN_NOT_ARMED);
    assert(send(PATROL_CAN_ARM,65534,0,0)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_TARGET,65535,0,5000)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_TARGET,0,0,-5000)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_STOP,65534,500,30000)==PATROL_CAN_OK);
    assert(!state()->enable_request && state()->target_rpm==0);

    init(); center_active=1; assert(send(PATROL_CAN_ARM,1,0,0)==PATROL_CAN_SAFETY);
    assert(send(PATROL_CAN_POWER_ON,2,0,0)==PATROL_CAN_SAFETY);
    center_active=0; ota=1; assert(send(PATROL_CAN_ARM,3,0,0)==PATROL_CAN_SAFETY);
    ota=0;
    for (unsigned gate=0;gate<9;gate++) {
        healthy(); ChassisFeedback f=state()->feedback;
        switch(gate) {
        case 0:f.calibrated=0;break; case 1:f.steering_online=0;break;
        case 2:f.drive_online=0;break; case 3:f.feedback_ms=now-251;break;
        case 4:f.drive_fault=1;break; case 5:f.can_bus_off=1;break;
        case 6:f.encoder_profile_ready=0;break; case 7:f.encoder_range_valid=0;break;
        case 8:f.encoder_actual_mdeg=2001;break;
        }
        RemoteService_UpdateFeedback(&f);
        assert(send(PATROL_CAN_ARM,(uint16_t)(10+gate),0,0)==PATROL_CAN_SAFETY);
        assert(!state()->session_active && !state()->enable_request);
        if (gate == 8)
            assert(state()->fault_latched && state()->stop_reason == CHASSIS_STOP_ANGLE_MISMATCH);
        else
            assert(!state()->fault_latched);
    }
    init();
    healthy(); assert(RemoteService_ApplyLocal("acquire","other-owner",1,300,0,0,now));
    assert(send(PATROL_CAN_ARM,50,0,0)==PATROL_CAN_OWNER);
    assert(!strcmp(state()->session_id,"other-owner"));
    assert(send(PATROL_CAN_POWER_OFF,0,0,0)==PATROL_CAN_OK);
    assert(!VehiclePowerOutput_IsOn() && !state()->enable_request);

    init(); assert(send(PATROL_CAN_ARM,1,0,0)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_TARGET,2,50,0)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_TARGET,3,-50,0)==PATROL_CAN_OK);
    assert(state()->target_rpm==0 && state()->reversal_pending);
    assert(send(PATROL_CAN_TARGET,4,0,0)==PATROL_CAN_OK);
    assert(!state()->reversal_pending && state()->active_direction==1);
    assert(send(PATROL_CAN_TARGET,5,-50,0)==PATROL_CAN_OK);
    RemoteService_SafetyTick(now); RemoteService_SafetyTick(now);
    assert(state()->zero_confirmations==0);
    for (unsigned n=0;n<3;n++) { now+=10; healthy(); RemoteService_SafetyTick(now); }
    assert(state()->target_rpm==-50 && !state()->reversal_pending);
    PatrolCanRemote_Tick(now,0);
    assert(state()->fault_latched && !state()->enable_request);
    assert(send(PATROL_CAN_POWER_OFF,6,0,0)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_RESET_FAULT,7,0,0)==PATROL_CAN_OK);
    assert(!state()->fault_latched);

    init(); assert(send(PATROL_CAN_ARM,1,0,0)==PATROL_CAN_OK);
    assert(send(PATROL_CAN_TARGET,2,50,0)==PATROL_CAN_OK);
    for (unsigned n=0;n<=60;n++) {
        ChassisFeedback f=state()->feedback; f.left_actual_rpm=f.right_actual_rpm=30;
        f.feedback_ms=now; RemoteService_UpdateFeedback(&f);
        assert(send(PATROL_CAN_TARGET,(uint16_t)(3+n),-50,0)==PATROL_CAN_OK);
        RemoteService_SafetyTick(now);
        if(n<60) {
            assert(!state()->fault_latched && state()->reversal_pending);
            now+=100;
        }
    }
    assert(state()->fault_latched && state()->stop_reason==CHASSIS_STOP_DIRECTION_TIMEOUT);
    assert(!state()->enable_request);
    drive_speed_cases();
    physical_b_ignored_cases();
    physical_steering_shaping_cases();
    calibration_cases();
    puts("CAN2 motion and native center-calibration bridge: PASS");
    return 0;
}
