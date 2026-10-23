#define CENTER_HOST_REAL_DIAGNOSTICS
#define CENTER_HOST_REAL_CONTROL
#define FAULT_STOP_REAL_REMOTE
#define FAULT_STOP_CUSTOM_TRANSPORT
#include "center_runtime_stubs.h"
#include "time_base.h"

// 本地遥控测试只使用单调毫秒，不创建网络时间授权或真实输出。
uint32_t HAL_GetTick(void) { return mock_now; }
// 不为本地模拟创造受信 UTC。
ABoxClockSnapshot TimeBase_GetSnapshot(void) { ABoxClockSnapshot s = {0}; return s; }
// 只提供链接所需的时间格式接口，不访问真实系统时钟。
uint8_t TimeBase_FormatUtcMs(char* output, uint16_t size)
{ return (uint8_t)(snprintf(output, size, "0") > 0); }
// 测试输出端口可用性，与真实接触器状态无关。
uint8_t VehiclePowerOutput_IsAvailable(void) { return 1U; }

typedef struct { PatrolCanFrame frame; uint32_t at; } Sent;
static Sent frames[3000];
static unsigned count, handled, triggers, dropped;
static int32_t target, profile, wheel[2], trigger_targets[20];
static uint16_t control_word[2], drop_index;
static uint8_t enabled[2], capacity, drop_node, omit_write, lose_verify_read, stuck_ack;
static uint8_t ack, bad_readback;
static uint8_t filter_value, wrong_mode, bad_width;
static int32_t drop_value;
static uint8_t drop_diagnostic_read, dropped_read;

// 模拟生产 Runtime 的邮箱提交；核验期间只能读，安全停机可以独立提交 shutdown/零速。
void PatrolRuntimePort_AbortCanTx(void) {}
// 只读模拟邮箱剩余容量。
uint8_t PatrolRuntimePort_CanTxReady(void) { return capacity != 0U; }
// 捕获真正提交的帧，区分排队目标与已发送目标。
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* f)
{
    assert(capacity && count < sizeof(frames)/sizeof(frames[0]));
    if (g_motion.bench.verify || g_motion.drive.verify) assert(f->data[0] == 0x40U);
    frames[count++] = (Sent){*f, mock_now};
    --capacity;
    return 1U;
}

// 把驱动器模拟值编码成真实宽度 SDO 响应；状态字使用现场 DLC6，不以写 ACK 代替状态确认。
static void upload_reply(PatrolCanFrame f, uint32_t now, int32_t value, uint8_t width)
{
    f.id -= 0x80U;
    f.data[0] = width == 1U ? 0x4FU : width == 2U ? 0x4BU : 0x43U;
    f.dlc = 4U + width;
    for (unsigned i = 0U; i < 4U; ++i) f.data[4U+i] = (uint8_t)((uint32_t)value >> (8U*i));
    PatrolRuntime_OnCanFrame(&f, now);
}

// 驱动器模拟可以独立丢 ACK、丢整个写入或丢核验响应，区分“已执行但没应答”和“从未执行”。
static void respond(void)
{
    unsigned end = count;
    while (handled < end) {
        PatrolCanFrame f = frames[handled++].frame;
        if (f.id == 0U) continue;
        uint8_t node = (uint8_t)(f.id - 0x600U);
        uint16_t index = f.data[1] | (uint16_t)f.data[2] << 8;
        assert(node == 1U || node == 2U);
        if (f.data[0] == 0x40U) {
            if (drop_diagnostic_read && !dropped_read && node == 1U && index == 0x6064U &&
                !g_motion.bench.verify && !g_motion.drive.verify) { dropped_read = 1U; continue; }
            if (lose_verify_read && (g_motion.bench.verify || g_motion.drive.verify)) continue;
            int32_t value = 0;
            uint8_t width = 4U;
            if (index == 0x6041U) {
                width = 2U;
                value = node == 1U ? (enabled[0] ? 0x2237 : 0x2231) : (enabled[1] ? 0x37 : 0x11);
                if (node == 1U && (ack || stuck_ack)) value |= 0x1000;
            } else if (index == 0x6061U) { width = 1U; value = wrong_mode ? 0 : node == 1U ? 1 : 3; }
            else if (index == 0x6040U) { width = 2U; value = control_word[node-1U]; }
            else if (index == 0x607AU) value = target + (bad_readback && g_motion.bench.verify ? 1 : 0);
            else if (index == 0x6081U) value = profile;
            else if (index == 0x60FFU) value = wheel[0];
            else if (index == 0x68FFU) value = wheel[1];
            else if (index == 0x6069U || index == 0x6869U)
                value = enabled[1] ? wheel[index == 0x6869U] : 0;
            else if (index >= 0x2000U && index <= 0x2006U) width = 2U;
            if (bad_width && (g_motion.bench.verify || g_motion.drive.verify)) width = width == 4U ? 2U : 4U;
            upload_reply(f, mock_now + 1U, value, width);
            continue;
        }
        int32_t value = (int32_t)((uint32_t)f.data[4] | (uint32_t)f.data[5] << 8 |
            (uint32_t)f.data[6] << 16 | (uint32_t)f.data[7] << 24);
        uint8_t drop = !dropped && node == drop_node && index == drop_index && (!filter_value || value == drop_value);
        if (!drop || !omit_write) {
            if (index == 0x6040U) {
                control_word[node-1U] = (uint16_t)value;
                if (value == 6) enabled[node-1U] = 0U;
                if (node == 1U && value == 0x3F) {
                    assert(!ack && triggers < sizeof(trigger_targets)/sizeof(trigger_targets[0]));
                    trigger_targets[triggers++] = target;
                    ack = 1U;
                }
                if (node == 1U && value == 0x2F) ack = 0U;
            } else if (index == 0x607AU) target = value;
            else if (index == 0x6081U) profile = value;
            else if (index == 0x60FFU) wheel[0] = value;
            else if (index == 0x68FFU) wheel[1] = value;
            else assert(0);
        }
        if (drop) { ++dropped; continue; }
        f.id -= 0x80U; f.data[0] = 0x60U; f.dlc = 8U;
        memset(f.data+4, 0, 4);
        PatrolRuntime_OnCanFrame(&f, mock_now + 1U);
    }
}

// 使用真实诊断、控制仲裁、Runtime 和双执行器；仅 CAN/时钟/配置端口模拟，不把主机结果当作实车验收。
static void fixture(void)
{
    memset(&g_motion, 0, sizeof(g_motion));
    memset(&mock_config, 0, sizeof(mock_config));
    count = handled = triggers = dropped = 0U;
    drop_node = omit_write = lose_verify_read = stuck_ack = ack = bad_readback = 0U;
    filter_value = wrong_mode = bad_width = 0U; drop_value = 0;
    drop_diagnostic_read = dropped_read = 0U;
    drop_index = 0U; mock_now = 1000U; mock_power = mock_profile_ready = 1U;
    mock_ota = mock_bus_off = 0U; capacity = 3U;
    target = 0; profile = 180; wheel[0] = 300; wheel[1] = -300;
    enabled[0] = enabled[1] = 1U; control_word[0] = 0x2FU; control_word[1] = 0x0FU;
    mock_config.calibrated = 1U;
    mock_config.steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
    mock_config.absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.steering_direction = mock_config.absolute_direction = 1;
    PatrolRuntime_Init(); RemoteService_Init();
    PatrolDiagnostics* d = &g_motion.diagnostics;
    d->steering_heartbeat_seen = d->drive_heartbeat_seen = 1U;
    d->steering_heartbeat_state = 5U; d->drive_heartbeat_state = 0x7FU;
    d->steering_heartbeat_ms = d->drive_heartbeat_ms = mock_now;
    d->steering_status_word = 0x2237U; d->drive_status_word = 0x37U;
    d->steering_status_ms = d->drive_status_ms = mock_now;
    d->steering_position_seen = d->left_rpm_seen = d->right_rpm_seen = d->encoder_seen = 1U;
    d->steering_position_ms = d->left_rpm_ms = d->right_rpm_ms = d->encoder_ms = mock_now;
    d->encoder_config_valid = 1U;
    d->left_actual_rpm = 300; d->right_actual_rpm = -300;
    g_motion.coordinate.valid = 1U;
    assert(BenchSteering_Start(&g_motion.bench, 0, 0, 0, 30000U, mock_now));
    g_motion.bench.minimum = -82680; g_motion.bench.maximum = 82680;
    g_motion.bench.phase = BENCH_ACTIVE; g_motion.bench.step = 16U; g_motion.bench.applied = 0;
    g_motion.bench.applied_profile_rpm = 180U;
    g_motion.drive.phase = BENCH_ACTIVE; g_motion.drive.step = 12U;
    g_motion.drive.applied = g_motion.drive.writing = 300; g_motion.drive.direction = 1;
    PatrolRuntime_UpdateFeedback(mock_now);
    assert(ChassisControl_SafetyReady(RemoteService_State(), mock_now));
    assert(RemoteService_ApplyLocal("acquire","verify-test",1U,1000U,300,0,mock_now));
}

// 连续接收在线和编码器采样，其他状态和轮速必须通过真实诊断 SDO 更新，不能直接为待测门禁续期。
static void tick(uint32_t now)
{
    mock_now = now; capacity = 3U;
    PatrolCanFrame heartbeat = {0}; heartbeat.dlc = 1U; heartbeat.id = 0x701U; heartbeat.data[0] = 5U;
    PatrolRuntime_OnCanFrame(&heartbeat, now);
    heartbeat.id = 0x702U; heartbeat.data[0] = 0x7FU;
    PatrolRuntime_OnCanFrame(&heartbeat, now);
    PatrolDiagnostics_OnEncoderCount(&g_motion.diagnostics, 0U, now);
    PatrolRuntime_Tick(); respond();
    RemoteService_SafetyTick(now + 1U);
}

// 在模拟遥控仍持续输入时核对最新目标取代待执行旧值；seq 仍走真实 RemoteService/ChassisControl 仲裁。
static void request(uint32_t seq, int16_t rpm, int32_t angle)
{
    assert(RemoteService_ApplyLocal("renew","verify-test",seq,1000U,rpm,angle,mock_now));
}

// 统计特定运动写入的实际提交次数，用以证明丢 ACK 的运动写入没有被重放。
static unsigned writes(uint8_t node, uint16_t index, int32_t value)
{
    unsigned total = 0U;
    for (unsigned i = 0U; i < count; ++i) {
        const PatrolCanFrame* f = &frames[i].frame;
        uint16_t key = f->data[1] | (uint16_t)f->data[2] << 8;
        int32_t sent = (int32_t)((uint32_t)f->data[4] | (uint32_t)f->data[5] << 8 |
            (uint32_t)f->data[6] << 16 | (uint32_t)f->data[7] << 24);
        if (f->id == 0x600U+node && key == index && f->data[0] != 0x40U && sent == value) ++total;
    }
    return total;
}

// 已执行的目标/触发/清触发丢 ACK 后，通过状态、模式、目标和控制字核验恢复；原始触发最多一次。
static void test_steering_lost_ack(uint16_t index, int32_t value)
{
    fixture(); drop_node = 1U; drop_index = index;
    if (index == 0x6040U) { filter_value = 1U; drop_value = value; }
    request(2U,300,4000);
    for (uint32_t t=1010U; t<1600U; t+=10U) tick(t);
    assert(dropped == 1U && !RemoteService_State()->fault_latched && !RemoteService_State()->fault_pending);
    assert(triggers == 1U && writes(1U,0x6040U,0x3F) == 1U && ack == 0U);
    assert(g_motion.bench.phase == BENCH_ACTIVE && !g_motion.bench.verify);
    printf("PASS steering missing ACK 0x%04X: read-only proof; no trigger replay\n", index);
}

// 未触发的旧转角在核验期间被更新时直接换成最新目标；不能恢复后补发积压旧定位。
static void test_latest_steering(void)
{
    fixture(); drop_node = 1U; drop_index = 0x607AU;
    request(2U,300,4000); uint8_t changed = 0U;
    for (uint32_t t=1010U; t<1600U; t+=10U) {
        tick(t);
        if (!changed && dropped && g_motion.bench.verify && g_motion.bench.verify != 5U) {
            request(3U,300,8000); changed = 1U;
        }
    }
    int32_t latest;
    assert(SteeringAngle_ToControllerCount(8000,0,VEHICLE_STEERING_COUNTS_MILLI,1,-82680,82680,&latest));
    assert(changed && dropped && triggers == 1U && trigger_targets[0] == latest);
    assert(!RemoteService_State()->fault_latched && target == latest);
    puts("PASS latest steering replaces superseded untriggered goal after verification");
}

// 行走左右目标分别丢 ACK，必须核对完整目标对和驱动状态；核验后采用最新遥控速度。
static void test_drive_lost_ack(uint16_t index)
{
    fixture(); drop_node = 2U; drop_index = index;
    request(2U,600,0); uint8_t changed = 0U;
    for (uint32_t t=1010U; t<1600U; t+=10U) {
        tick(t);
        if (!changed && g_motion.drive.verify) { request(3U,400,0); changed = 1U; }
    }
    assert(changed && dropped && !RemoteService_State()->fault_latched);
    assert(wheel[0] == 400 && wheel[1] == -400 && g_motion.drive.applied == 400);
    assert(writes(2U,index,index == 0x60FFU ? 600 : -600) == 1U);
    if (index == 0x60FFU) assert(!writes(2U,0x68FFU,-600));
    printf("PASS drive missing ACK 0x%04X: full pair verified; latest speed only\n", index);
}

// 不执行写入、核验漏响应、错误回读或旧 ACK 一直为高时均须安全停机，不能靠新遥控帧解除故障。
static void test_verification_failure(unsigned kind)
{
    fixture(); drop_node = 1U; drop_index = 0x607AU;
    omit_write = kind == 0U; lose_verify_read = kind == 1U;
    bad_readback = kind == 2U; stuck_ack = kind == 3U;
    wrong_mode = kind == 4U; bad_width = kind == 5U;
    request(2U,300,4000);
    for (uint32_t t=1010U; t<1700U; t+=10U) tick(t);
    assert(RemoteService_State()->fault_latched && !RemoteService_State()->enable_request);
    assert(!enabled[0] && !enabled[1] && wheel[0] == 0 && wheel[1] == 0);
    assert(writes(1U,0x6040U,6) && writes(2U,0x6040U,6));
    assert(!triggers && !RemoteService_ApplyLocal("renew","verify-test",9U,1000U,600,8000,mock_now));
    printf("PASS verification failure %u: zero targets, both disabled, no automatic resume\n", kind);
}

// 核验途中出现 OTA、Bus-Off 或过期反馈时仍服从原安全门禁，不因恢复流程而继续运动。
static void test_safety_gate_during_verify(unsigned kind)
{
    fixture(); drop_node = 2U; drop_index = 0x60FFU;
    request(2U,600,0); uint8_t injected = 0U;
    for (uint32_t t=1010U; t<1800U; t+=10U) {
        tick(t);
        if (!injected && g_motion.drive.verify) {
            injected = 1U;
            if (kind == 0U) mock_ota = 1U;
            else if (kind == 1U) mock_bus_off = 1U;
            else g_motion.diagnostics.steering_position_ms = t-251U;
        }
        if (injected && t>1400U) mock_bus_off = 0U;
    }
    assert(injected && RemoteService_State()->fault_latched && !enabled[0] && !enabled[1]);
    assert(wheel[0] == 0 && wheel[1] == 0 && !RemoteService_State()->session_active);
    printf("PASS safety gate %u during verification still stops both axes\n", kind);
}

// 真实 Runtime 中单次诊断读取漏响应只计数并有限重读，不撤销仍安全的遥控会话或永久锁存。
static void test_single_diagnostic_loss(void)
{
    fixture(); drop_diagnostic_read = 1U;
    for (uint32_t t=1010U; t<1600U; t+=10U) tick(t);
    assert(dropped_read && g_motion.diagnostics.sdo_timeout_count == 1U);
    assert(!RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched);
    assert(RemoteService_State()->session_active && RemoteService_State()->enable_request);
    assert(g_motion.drive.phase == BENCH_ACTIVE && g_motion.bench.phase == BENCH_ACTIVE);
    puts("PASS single diagnostic read loss: one retry recorded; no permanent latch or lost session");
}

// 活动轮廓速度写入丢 ACK 也要核对模式、目标和实际寄存器，不能把运动触发当作重试手段。
static void test_profile_lost_ack(void)
{
    fixture(); drop_node = 1U; drop_index = 0x6081U;
    for (uint32_t t=1010U; t<1600U; t+=10U) tick(t);
    assert(dropped && !RemoteService_State()->fault_latched && profile == 90 && !triggers);
    assert(writes(1U,0x6081U,90) == 1U);
    puts("PASS active profile write ACK loss verified without motion replay");
}

// 已发出的触发结果未知时，新目标不能取消核验；必须先完成原触发证明并清触发，再执行最新目标。
static void test_latest_after_unknown_trigger(void)
{
    fixture(); drop_node = 1U; drop_index = 0x6040U;
    filter_value = 1U; drop_value = 0x3F;
    request(2U,300,4000); uint8_t changed = 0U;
    for (uint32_t t=1010U; t<1700U; t+=10U) {
        tick(t);
        if (!changed && dropped && g_motion.bench.verify) { request(3U,300,8000); changed = 1U; }
    }
    assert(changed && triggers == 2U && trigger_targets[0] != trigger_targets[1]);
    uint8_t proved = 0U;
    unsigned target_writes = 0U;
    for (unsigned i=0U; i<count; ++i) {
        const PatrolCanFrame* f=&frames[i].frame;
        if (f->id != 0x601U) continue;
        uint16_t index = f->data[1] | (uint16_t)f->data[2] << 8;
        if (f->data[0] == 0x40U && index == 0x6040U) proved = 1U;
        if (f->data[0] != 0x40U && index == 0x607AU && ++target_writes > 1U) assert(proved);
    }
    assert(proved && !RemoteService_State()->fault_latched);
    puts("PASS new target cannot cancel proof of an already-sent unknown trigger");
}

// 直接编码局部行走执行器的只读回复，用来验证严格期限而不依赖主机调度耗时。
static void drive_reply(BenchDrive* b, PatrolCanFrame f, int32_t value, uint32_t now)
{
    f.id = 0x582U; f.data[0] = b->width == 1U ? 0x4FU : b->width == 2U ? 0x4BU : 0x43U;
    f.dlc = 4U + b->width;
    for (unsigned i=0U; i<4U; ++i) f.data[4U+i]=(uint8_t)((uint32_t)value >> (8U*i));
    assert(BenchDrive_OnFrame(b,&f,now));
}

// 39/40ms ACK 边界、79/80ms 核验总期限和毫秒回绕都不能被新响应续期或转成假成功。
static void test_verification_deadlines(void)
{
    for (unsigned wrap=0U; wrap<2U; ++wrap) {
        uint32_t base=wrap ? UINT32_MAX-50U : 1000U;
        BenchDrive b; PatrolCanFrame f;
        BenchDrive_Init(&b); b.phase=BENCH_ACTIVE; b.step=13U; b.writing=b.desired=600; b.applied=300;
        assert(BenchDrive_Take(&b,base,&f));
        BenchDrive_Tick(&b,base+39U,1U,1U,600,300,-300,base,base);
        assert(b.pending && !b.verify && b.phase==BENCH_ACTIVE);
        BenchDrive_Tick(&b,base+40U,1U,1U,600,300,-300,base,base);
        assert(b.verify && !b.pending);
        assert(BenchDrive_Take(&b,base+40U,&f)); drive_reply(&b,f,0x37,base+69U);
        assert(BenchDrive_Take(&b,base+70U,&f)); drive_reply(&b,f,3,base+99U);
        assert(BenchDrive_Take(&b,base+100U,&f)); drive_reply(&b,f,600,base+119U);
        assert(b.phase==BENCH_ACTIVE && b.verify);
        assert(!BenchDrive_Take(&b,base+120U,&f));
        assert(b.phase==BENCH_FAULT && b.cut_power);
    }
    puts("PASS ACK 39/40ms and verification 79/80ms boundaries across wrap");
}

// 丢零速写 ACK 的换向流程仍须确认双零目标之后的三组新低速反馈，旧零速缓存不能跳过刹车。
static void test_reversal_after_verified_zero(void)
{
    BenchDrive b; PatrolCanFrame f;
    BenchDrive_Init(&b); b.phase=BENCH_ACTIVE; b.step=12U; b.applied=b.writing=300; b.direction=1;
    BenchDrive_Tick(&b,1000U,1U,1U,-300,300,-300,1000U,1000U);
    assert(b.reversing && !b.writing && BenchDrive_Take(&b,1000U,&f));
    BenchDrive_Tick(&b,1040U,1U,1U,-300,0,0,1000U,1000U);
    assert(BenchDrive_Take(&b,1041U,&f)); drive_reply(&b,f,0x37,1042U);
    assert(BenchDrive_Take(&b,1043U,&f)); drive_reply(&b,f,3,1044U);
    assert(BenchDrive_Take(&b,1045U,&f)); drive_reply(&b,f,0,1046U);
    assert(BenchDrive_Take(&b,1047U,&f)); drive_reply(&b,f,-300,1048U);
    assert(BenchDrive_Take(&b,1049U,&f) && f.data[1]==0xFFU && f.data[2]==0x68U);
    f.id=0x582U; f.data[0]=0x60U;
    assert(BenchDrive_OnFrame(&b,&f,1050U));
    BenchDrive_Tick(&b,1051U,1U,1U,-300,0,0,1050U,1050U);
    assert(b.reversing && b.confirmations==0U);
    for (uint32_t sample=1052U; sample<=1054U; ++sample)
        BenchDrive_Tick(&b,sample,1U,1U,-300,0,0,sample,sample);
    assert(!b.reversing && b.writing==-300 && b.step==13U);
    puts("PASS verified zero does not bypass three fresh stopped feedback pairs");
}

// 执行生产路径的成功、未知执行结果、最新目标和安全退路场景。
int main(void)
{
    _Static_assert(sizeof(BenchSteering)==88U && sizeof(BenchDrive)==52U,"verification must use former padding");
    test_single_diagnostic_loss();
    test_steering_lost_ack(0x607AU,0);
    test_steering_lost_ack(0x6040U,0x3F);
    test_steering_lost_ack(0x6040U,0x2F);
    test_profile_lost_ack();
    test_latest_steering();
    test_latest_after_unknown_trigger();
    test_drive_lost_ack(0x60FFU);
    test_drive_lost_ack(0x68FFU);
    for (unsigned kind=0U; kind<6U; ++kind) test_verification_failure(kind);
    for (unsigned kind=0U; kind<3U; ++kind) test_safety_gate_during_verify(kind);
    test_verification_deadlines();
    test_reversal_after_verified_zero();
    return 0;
}
