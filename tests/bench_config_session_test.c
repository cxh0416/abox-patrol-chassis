#include "vcu_config.h"
#include "remote_service.h"
#include "cloud_protocol.h"
#include "vehicle_power_output.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static VcuRemoteConfig_t flash;
static uint8_t valid, save_ok=1;
static uint32_t tick=1000;
static uint8_t power_level;
uint32_t HAL_GetTick(void) { return tick; }
uint8_t TimeBase_FormatUtcMs(char* s,uint16_t n) { snprintf(s,n,"1788838333000"); return 1; }
uint8_t OTA_Info_LoadRemoteConfig(VcuRemoteConfig_t* c) { if(valid)*c=flash; return valid; }
uint8_t OTA_Info_SaveRemoteConfig(const VcuRemoteConfig_t* c) { if(!save_ok)return 0; flash=*c;valid=1;return 1; }
static uint8_t write_power(uint8_t on) { power_level=on; return 1; }
static void command(const char* session,int rpm,int angle,const char* action,int seq)
{
    char json[512],response[512];
    snprintf(json,sizeof(json),"{\"version\":\"3.0\",\"data\":{\"deviceType\":\"vehicle_chassis\",\"requestId\":\"r%d\",\"command\":\"chassis_control\",\"params\":{\"action\":\"%s\",\"sessionId\":\"%s\",\"seq\":%d,\"leaseMs\":1000,\"driveRpm\":%d,\"steeringDeg\":%d}}}",seq,action,session,seq,rpm,angle);
    assert(RemoteService_HandleControl(json,strlen(json),response,sizeof(response)));
}
int main(void)
{
    CloudProtocolRequest_t req;
    const char* json="{\"version\":\"2.0\",\"data\":{\"requestId\":\"center\",\"command\":\"internal_config\",\"params\":{\"action\":\"set_calibration\",\"steeringCenterCount\":-85123,\"absoluteCenterCount\":3182}}}";
    assert(CloudProtocol_Parse(json,strlen(json),0,&req)==CLOUD_PROTOCOL_OK);
    assert(req.steering_counts_per_degree_milli==2710800&&req.absolute_counts_per_degree_milli==55250);
    VehiclePowerOutput_Init(write_power);
    VcuConfig_Load(); assert(!VcuConfig_Get()->calibrated&&VcuConfig_CalibrationCompatible());
    save_ok=0; assert(!VcuConfig_SetCalibration(-85123,3182,2710800,55250,1,1,2000));
    assert(!VcuConfig_Get()->calibrated);
    save_ok=1; assert(VcuConfig_SetCalibration(-85123,3182,2710800,55250,1,1,2000));
    assert(!VcuConfig_SingleTurnProfileReady());
    assert(VcuConfig_SetCalibration(-85123,0,2710800,55250,1,1,2000));
    assert(VcuConfig_SingleTurnProfileReady());
    assert(VcuConfig_SetCalibration(-85123,3182,2710800,55250,1,1,2000));
    VcuConfig_Load(); assert(VcuConfig_Get()->calibrated&&!VcuConfig_Get()->remote_enabled);
    flash.steering_counts_per_degree_milli=555556; VcuConfig_Load();
    assert(!VcuConfig_CalibrationCompatible()&&VcuConfig_Get()->steering_center_count==-85123);
    flash.steering_counts_per_degree_milli=2710800;VcuConfig_Load();
    RemoteService_Init(); assert(!RemoteService_BenchPermitted());
    ChassisFeedback f={.calibrated=1,.steering_online=1,.drive_online=1,.power_ready=1,
        .encoder_range_valid=1,.encoder_profile_ready=1,.feedback_ms=1000};
    RemoteService_UpdateFeedback(&f);
    assert(RemoteService_BenchArm(1000,tick));
    char session[40]; strcpy(session,RemoteService_BenchSession());
    command(session,1,0,"acquire",1); assert(!RemoteService_State()->session_active);
    command(session,0,2,"acquire",2); assert(!RemoteService_State()->session_active);
    command(session,0,1,"acquire",3); assert(RemoteService_State()->enable_request);
    tick=2001;f.feedback_ms=tick;RemoteService_UpdateFeedback(&f);RemoteService_SafetyTick(tick);
    assert(!RemoteService_BenchPermitted()&&!RemoteService_State()->enable_request);
    command(session,0,1,"renew",4);assert(!RemoteService_State()->enable_request);
    assert(RemoteService_BenchArm(1000,tick));
    command(session,0,1,"acquire",5);assert(!RemoteService_State()->enable_request);
    strcpy(session,RemoteService_BenchSession());
    command(session,0,1,"acquire",6);assert(RemoteService_State()->enable_request);
    command(session,0,0,"stop",7);assert(!RemoteService_BenchPermitted());
    assert(RemoteService_BenchArm(1000,tick));
    tick+=15001;f.feedback_ms=tick;RemoteService_UpdateFeedback(&f);RemoteService_SafetyTick(tick);
    assert(!RemoteService_BenchPermitted());
    RemoteService_BenchFault();
    RemoteService_SafetyTick(tick);
    assert(RemoteService_State()->fault_latched);
    assert(RemoteService_State()->stop_reason==CHASSIS_STOP_EXECUTION_FAULT);
    assert(!RemoteService_BenchArm(1000,tick));
    command(session,0,0,"stop",8);
    assert(RemoteService_State()->stop_reason==CHASSIS_STOP_EXECUTION_FAULT);
    RemoteService_Init();assert(!RemoteService_State()->remote_enabled);
    RemoteService_UpdateFeedback(&f);
    assert(RemoteService_BenchDriveArm(30000,tick));
    strcpy(session,RemoteService_BenchSession());
    command(session,201,0,"acquire",20); assert(!RemoteService_State()->session_active);
    command(session,200,30,"acquire",21); assert(RemoteService_State()->enable_request);
    command(session,0,0,"stop",22); assert(!RemoteService_BenchDrivePermitted());
    assert(RemoteService_BenchArm(1000,tick));
    strcpy(session,RemoteService_BenchSession());
    command(session,30,0,"acquire",23); assert(!RemoteService_State()->session_active);
    {
        char response[512];
        const char* power_on="{\"version\":\"3.0\",\"data\":{\"deviceType\":\"vehicle_chassis\",\"requestId\":\"power-1\",\"command\":\"power_on\",\"params\":null}}";
        const char* power_conflict="{\"version\":\"3.0\",\"data\":{\"deviceType\":\"vehicle_chassis\",\"requestId\":\"power-1\",\"command\":\"power_off\",\"params\":null}}";
        const char* power_off="{\"version\":\"3.0\",\"data\":{\"deviceType\":\"vehicle_chassis\",\"requestId\":\"power-2\",\"command\":\"power_off\",\"params\":null}}";
        assert(RemoteService_HandleControl(power_on,strlen(power_on),response,sizeof(response)));
        assert(power_level==1 && strstr(response,"\"code\":200") && strstr(response,"\"powerOutputOn\":true"));
        assert(RemoteService_HandleControl(power_conflict,strlen(power_conflict),response,sizeof(response)));
        assert(power_level==1 && strstr(response,"\"code\":4001") && strstr(response,"\"powerOutputOn\":true"));
        assert(RemoteService_HandleControl(power_on,strlen(power_on),response,sizeof(response)));
        assert(power_level==1 && strstr(response,"\"code\":200") && strstr(response,"\"powerOutputOn\":true"));
        assert(RemoteService_HandleControl(power_off,strlen(power_off),response,sizeof(response)));
        assert(power_level==0 && strstr(response,"\"code\":200") && strstr(response,"\"powerOutputOn\":false"));
    }
    puts("PASS profile persistence, failed save, compatibility, steering-only permit, stop and replay rejection");
}
