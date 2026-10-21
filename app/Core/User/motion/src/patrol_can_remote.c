#include "patrol_can_remote.h"
#include "remote_service.h"
#include "center_calibration.h"
#include "patrol_runtime.h"
#include "patrol_runtime_port.h"
#include "vehicle_power_output.h"
#include <limits.h>
#include <string.h>

static const char session[] = "can2-keyboard";
static uint32_t control_seq;
static uint16_t last_seq, reply_seq;
static uint8_t armed, have_sequence, reply_pending, reply_code;

// 按小端顺序读取 CAN 载荷中的 16 位数值。输入至少包含两个可读字节；返回未作符号扩展的无符号值。
static uint16_t u16(const uint8_t* p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
// 按小端顺序将 16 位数值写入 CAN 载荷。调用方保证两个输出字节可写，符号解释由协议字段类型决定。
static void put16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
// 判断 CAN2 键盘控制入口是否持有共享会话。通过固定会话标识检查归属，不以收到某条命令代替控制权确认。
static uint8_t owns(void)
{
    const ChassisControl* c=RemoteService_State();
    return c->session_active && !strcmp(c->session_id,session);
}
// 通过共享仲裁请求 CAN2 远控停止。撤销该入口目标和武装状态，实际驱动器停车由运动运行层完成。
static void stop(uint32_t now)
{
    (void)RemoteService_ApplyLocal("stop",session,0,0,0,0,now);
    armed=0U;
}
// 清空 CAN2 命令入口序号、武装和待响应状态。初始化不依赖 MQTT 或 UTC，仍须在后续 ARM 命令中通过安全门禁。
void PatrolCanRemote_Init(void)
{
    control_seq=0; last_seq=reply_seq=0; armed=have_sequence=reply_pending=reply_code=0;
}
// 监督 CAN2 远控租约与底层通信状态。在失联或通信故障时撤销会话，故障按现有安全仲裁记录；不得借续租延续陈旧目标。
void PatrolCanRemote_Tick(uint32_t now, uint8_t healthy)
{
    const ChassisControl* c=RemoteService_State();
    if (!armed) return;
    if (!healthy) {
        stop(now);
        RemoteService_ExecutionFault();
        return;
    }
    if (!healthy || CenterCalibration_IsActive() || !owns() ||
        !c->enable_request || (int32_t)(now-c->lease_deadline_ms)>=0 ||
        !ChassisControl_SafetyReady(c,now)) {
        stop(now);
    }
}
// 解析 CAN2 本地控制命令并执行仲裁。校验帧格式、版本、序号和接收年龄，分别处理查询、供电、使能、目标、停止及复位；运动权限仍由共享服务决定。
void PatrolCanRemote_OnFrame(const PatrolCanFrame* f, uint32_t received, uint32_t now)
{
    uint16_t seq,delta;
    uint8_t op;
    int16_t rpm,angle;
    const ChassisControl* c;
    if (!f || f->id!=PATROL_CAN_REMOTE_COMMAND || f->dlc!=8 ||
        f->data[0]!=PATROL_CAN_REMOTE_VERSION) return;
    op=f->data[1]; seq=u16(f->data+2); rpm=(int16_t)u16(f->data+4); angle=(int16_t)u16(f->data+6);
    reply_seq=seq; reply_pending=1; reply_code=PATROL_CAN_INVALID;
    if (op>PATROL_CAN_RESET_FAULT) return;
    /* Safety stop/off always take effect, even with an old sequence. */
    if (op==PATROL_CAN_STOP || op==PATROL_CAN_POWER_OFF) {
        stop(now);
        reply_code=PATROL_CAN_OK;
        if (op==PATROL_CAN_POWER_OFF) {
            RemoteService_Revoke();
            if (!VehiclePowerOutput_Set(0)) reply_code=PATROL_CAN_UNAVAILABLE;
        }
        return;
    }
    if ((uint32_t)(now-received)>100U) { reply_code=PATROL_CAN_STALE; return; }
    if (op==PATROL_CAN_QUERY) { reply_code=PATROL_CAN_OK; return; }
    if ((op!=PATROL_CAN_TARGET && (rpm || angle)) || rpm>CHASSIS_MAX_RPM || rpm<-CHASSIS_MAX_RPM || angle>30000 || angle<-30000) return;
    c=RemoteService_State();
    if (CenterCalibration_IsActive() || PatrolRuntimePort_OtaBusy()) { reply_code=PATROL_CAN_SAFETY; return; }
    if (c->session_active && !owns()) { reply_code=PATROL_CAN_OWNER; return; }
    if (op==PATROL_CAN_POWER_ON) {
        if (c->session_active) { reply_code=PATROL_CAN_OWNER; return; }
        reply_code=VehiclePowerOutput_Set(1) ? PATROL_CAN_OK : PATROL_CAN_UNAVAILABLE;
        return;
    }
    if (op==PATROL_CAN_RESET_FAULT) {
        reply_code=PatrolRuntime_ResetFault() ? PATROL_CAN_OK : PATROL_CAN_SAFETY;
        return;
    }
    if (op==PATROL_CAN_ARM) {
        if (armed || owns()) { reply_code=PATROL_CAN_OWNER; return; }
        if (RemoteService_ApplyLocal("acquire",session,++control_seq,PATROL_CAN_REMOTE_LEASE_MS,0,0,now)) {
            armed=have_sequence=1; last_seq=seq; reply_code=PATROL_CAN_OK;
        } else reply_code=PATROL_CAN_SAFETY;
        return;
    }
    if (!armed || !owns() || (int32_t)(now-c->lease_deadline_ms)>=0) {
        if (owns()) stop(now);
        armed=0; reply_code=PATROL_CAN_NOT_ARMED; return;
    }
    delta=(uint16_t)(seq-last_seq);
    if (!have_sequence || !delta || delta>=0x8000U) { reply_code=PATROL_CAN_SEQUENCE; return; }
    if (!RemoteService_ApplyLocal("renew",session,++control_seq,PATROL_CAN_REMOTE_LEASE_MS,rpm,angle,now)) {
        stop(now); reply_code=PATROL_CAN_SAFETY; return;
    }
    last_seq=seq; reply_code=PATROL_CAN_OK;
}
// 取出一组关联同一请求序号的三帧响应。将当前结果和安全状态编码到固定响应 ID；没有待响应时返回零，响应成功不表示真实运动完成。
uint8_t PatrolCanRemote_TakeReply(PatrolCanFrame frames[3], uint32_t now)
{
    const ChassisControl* c=RemoteService_State();
    uint32_t age=now-c->feedback.feedback_ms;
    if (!reply_pending || !frames) return 0;
    reply_pending=0;
    memset(frames,0,3*sizeof(*frames));
    for (unsigned i=0;i<3;i++) { frames[i].id=(uint16_t)(PATROL_CAN_REMOTE_ACK+i); frames[i].dlc=8; }
    frames[0].data[0]=PATROL_CAN_REMOTE_VERSION; frames[0].data[1]=reply_code;
    put16(frames[0].data+2,reply_seq);
    frames[0].data[4]=(uint8_t)((VehiclePowerOutput_IsOn()?1:0)|(c->feedback.calibrated?2:0)|
        (ChassisControl_SafetyReady(c,now)?4:0)|(c->fault_latched?8:0)|(armed&&owns()?16:0)|
        (c->session_active?32:0)|(c->feedback.ota_busy?64:0)|(c->feedback.can_bus_off?128:0));
    frames[0].data[5]=(uint8_t)c->stop_reason;
    put16(frames[0].data+6,(uint16_t)(age>UINT16_MAX?UINT16_MAX:age));
    put16(frames[1].data,reply_seq);
    put16(frames[1].data+2,(uint16_t)(int16_t)c->feedback.steering_actual_mdeg);
    put16(frames[1].data+4,(uint16_t)(int16_t)c->feedback.encoder_actual_mdeg);
    frames[1].data[6]=c->feedback.steering_online; frames[1].data[7]=c->feedback.drive_online;
    put16(frames[2].data,reply_seq);
    put16(frames[2].data+2,(uint16_t)c->feedback.left_actual_rpm);
    put16(frames[2].data+4,(uint16_t)c->feedback.right_actual_rpm);
    frames[2].data[6]=c->reversal_pending; frames[2].data[7]=c->enable_request;
    return 1;
}
