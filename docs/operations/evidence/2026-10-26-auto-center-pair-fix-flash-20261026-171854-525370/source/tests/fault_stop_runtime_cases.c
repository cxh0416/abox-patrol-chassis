#define CENTER_HOST_REAL_CONTROL
#define FAULT_STOP_REAL_REMOTE
#define FAULT_STOP_CUSTOM_TRANSPORT
#define FAULT_STOP_CUSTOM_LOCKS
#include "center_runtime_stubs.h"
#include "patrol_fault_stop.h"
#include "time_base.h"

typedef struct {
    PatrolCanFrame frame;
    uint32_t at;
    uint8_t pending, latched, power;
} Sent;
static Sent sent[1024];
static unsigned sent_count, processed, aborts;
static uint8_t capacity, refuse_send, inject_fault, drop_node, drop_shutdown_ack, ignore_shutdown_node;
static uint8_t enabled[2];
static uint32_t delayed_shutdown_until;
static int32_t left_target, right_target;

// 主机端口只模拟时钟和邮箱；RemoteService、ChassisControl 和停机执行器均链接生产代码。
uint32_t HAL_GetTick(void) { return mock_now; }
// 本夹具只验证本地故障流程，不创建受信 UTC 或网络控制许可。
ABoxClockSnapshot TimeBase_GetSnapshot(void) { ABoxClockSnapshot s = {0}; return s; }
uint8_t TimeBase_FormatUtcMs(char* output, uint16_t size)
{ return (uint8_t)(snprintf(output, size, "0") > 0); }
uint8_t VehiclePowerOutput_IsAvailable(void) { return 1U; }
void PatrolRuntimePort_Enter(void) {}
void PatrolRuntimePort_Exit(void)
{
    if (inject_fault && g_motion.drive.pending && g_motion.drive.phase == BENCH_ACTIVE) {
        inject_fault = 0U;
        RemoteService_ExecutionFault();
    }
}
void PatrolRuntimePort_AbortCanTx(void) { ++aborts; }
uint8_t PatrolRuntimePort_CanTxReady(void) { return capacity != 0U; }
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* frame)
{
    if (refuse_send) return 0U;
    assert(capacity && sent_count < sizeof(sent)/sizeof(sent[0]));
    const ChassisControl* c = RemoteService_State();
    sent[sent_count++] = (Sent){*frame, mock_now, c->fault_pending, c->fault_latched, mock_power};
    --capacity;
    return 1U;
}

// 所有故障停机输出都必须属于白名单；尤其不能重放定位触发、使能、非零速度或调参对象。
static void assert_safe_frame(const PatrolCanFrame* f)
{
    assert(f->id == 0x601U || f->id == 0x602U);
    uint16_t index = (uint16_t)f->data[1] | (uint16_t)f->data[2] << 8;
    assert(f->data[3] == 0U);
    if (index == 0x6040U) assert(f->data[0] == 0x2BU && f->data[4] == 6U && !f->data[5]);
    else if (index == 0x6041U) assert(f->data[0] == 0x40U);
    else {
        assert(f->id == 0x602U && (index == 0x60FFU || index == 0x68FFU));
        assert(f->data[0] == 0x23U && !f->data[4] && !f->data[5] && !f->data[6] && !f->data[7]);
    }
}

// 模拟驱动器处理安全命令，并可独立丢弃一个节点的 ACK/全部响应；不把模拟执行当作真实电机验收。
static void respond(void)
{
    unsigned end = sent_count;
    while (processed < end) {
        PatrolCanFrame request = sent[processed++].frame;
        assert_safe_frame(&request);
        uint8_t node = (uint8_t)(request.id - 0x600U);
        uint16_t index = request.data[1] | (uint16_t)request.data[2] << 8;
        uint8_t read = request.data[0] == 0x40U;
        if (index == 0x6040U && node != ignore_shutdown_node &&
            (node != 1U || !delayed_shutdown_until)) enabled[node - 1U] = 0U;
        if (node == 1U && delayed_shutdown_until && mock_now >= delayed_shutdown_until) enabled[0] = 0U;
        if (index == 0x60FFU) left_target = 0;
        if (index == 0x68FFU) right_target = 0;
        if (node == drop_node || (drop_shutdown_ack && node == 1U && index == 0x6040U)) continue;
        PatrolCanFrame reply = request;
        reply.id -= 0x80U;
        reply.data[0] = read ? 0x4BU : 0x60U;
        reply.dlc = read ? 6U : 8U; /* 真实 VSY 状态回复允许 DLC6。 */
        memset(reply.data + 4, 0, 4);
        if (read) reply.data[4] = enabled[node - 1U] ? 0x27U : 0x21U;
        PatrolRuntime_OnCanFrame(&reply, mock_now + 1U);
    }
}

// 每个场景使用相同 1000 rpm/使能状态起点；状态反馈保持新鲜，避免用其他门禁代替待验证故障。
static void fixture(void)
{
    memset(&g_motion, 0, sizeof(g_motion));
    memset(&mock_config, 0, sizeof(mock_config));
    memset(&mock_snapshot, 0, sizeof(mock_snapshot));
    mock_now = 1000U; mock_power = mock_profile_ready = 1U;
    mock_bus_off = mock_ota = 0U;
    capacity = 3U; refuse_send = inject_fault = drop_node = drop_shutdown_ack = ignore_shutdown_node = 0U;
    sent_count = processed = aborts = 0U;
    delayed_shutdown_until = 0U;
    enabled[0] = enabled[1] = 1U;
    left_target = 1000; right_target = -1000;
    mock_config.calibrated = 1U;
    mock_config.steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
    mock_config.absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.steering_direction = mock_config.absolute_direction = 1;
    PatrolRuntime_Init();
    RemoteService_Init();
    ChassisFeedback f = {0};
    f.calibrated = f.steering_online = f.drive_online = f.power_ready = 1U;
    f.encoder_profile_ready = f.encoder_range_valid = 1U;
    f.angle_pair_valid = 1U;
    f.feedback_ms = mock_now;
    RemoteService_UpdateFeedback(&f);
    assert(RemoteService_ApplyLocal("acquire", "fault-test", 1U, 1000U, 1000, 8000, mock_now));
    g_motion.coordinate.valid = 1U;
    g_motion.coordinate_calibration_center = mock_config.steering_center_count;
    assert(BenchSteering_Start(&g_motion.bench, 0, 0, 0, 30000U, mock_now));
    g_motion.bench.minimum = -82680; g_motion.bench.maximum = 82680;
    g_motion.bench.phase = g_motion.drive.phase = BENCH_ACTIVE;
    g_motion.bench.step = 16U;
    g_motion.drive.step = 12U;
    g_motion.drive.writing = 1000;
    g_motion.diagnostics.steering_status_word = g_motion.diagnostics.drive_status_word = 0x27U;
    g_motion.diagnostics.steering_status_ms = g_motion.diagnostics.drive_status_ms = mock_now;
    g_motion.diagnostics.left_actual_rpm = 997; g_motion.diagnostics.right_actual_rpm = -1000;
}

// 停机期间每次调度前恢复三邮箱容量；外部响应与提交分开，避免同步假 ACK 掩盖并发问题。
static void tick(uint32_t now)
{
    mock_now = now; capacity = 3U;
    PatrolRuntime_Tick();
    respond();
}

// 验证两个 shutdown 都先于锁存提交，pending 阶段拒绝重新接管，确认后锁存/下电并保留首故障快照。
static void test_confirm_before_latch(void)
{
    fixture();
    RemoteService_ExecutionFault();
    assert(sent_count == 2U && aborts == 1U);
    assert(sent[0].frame.id == 0x602U && sent[1].frame.id == 0x601U);
    for (unsigned i = 0; i < 2U; ++i) assert(sent[i].pending && !sent[i].latched && sent[i].power);
    assert(RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched);
    assert(!RemoteService_State()->enable_request && !RemoteService_State()->session_active);
    assert(!RemoteService_ApplyLocal("acquire", "new-owner", 1U, 1000U, 0, 0, mock_now));
    assert(!PatrolRuntime_ResetFault());
    MotionFaultRecord first, last;
    assert(PatrolRuntime_GetFault(&first));
    respond();
    for (uint32_t t = 1010U; t <= 1100U; t += 10U) tick(t);
    assert(!enabled[0] && !enabled[1] && !left_target && !right_target);
    assert(RemoteService_State()->fault_latched && !RemoteService_State()->fault_pending && !mock_power);
    assert(PatrolRuntime_GetFault(&last) && !memcmp(&first,&last,sizeof(first)));
    assert(PatrolRuntime_ResetFault());
    puts("PASS both shutdowns before latch; fresh DLC6 status; first cause preserved");
}

// Abort 没有事务号；核验超时后迟到的旧 upload Abort 不能被当作 shutdown 已处理。
static void test_late_abort_retries_same_safe_write(void)
{
    PatrolFaultStop stop;
    PatrolCanFrame frame;
    PatrolFaultStop_Init(&stop);
    PatrolFaultStop_Begin(&stop, PATROL_FAULT_STOP_STEERING, 1000U);
    assert(PatrolFaultStop_Take(&stop,1000U,&frame));
    assert_safe_frame(&frame);
    PatrolCanFrame late_abort = {
        .id = 0x581U, .dlc = 8U, .data = {0x80U,0x40U,0x60U,0U,0U,0U,0U,0U}
    };
    assert(PatrolFaultStop_OnFrame(&stop,&late_abort,1001U));
    assert(!PatrolFaultStop_Take(&stop,1010U,&frame));
    assert(PatrolFaultStop_Take(&stop,1011U,&frame));
    assert_safe_frame(&frame);
    assert(frame.data[0] == 0x2BU && frame.data[4] == 6U);
    puts("PASS late Abort retries shutdown instead of advancing the safe sequence");
}

// 丢弃转向全部响应时，行走仍应先撤使能。599 ms 不提前锁存，600 ms 进入失败退路，后续只重试安全命令。
static void test_missing_node_and_deadline(void)
{
    fixture(); drop_node = 1U;
    RemoteService_ExecutionFault(); respond();
    for (uint32_t t = 1010U; t < 1590U; t += 10U) tick(t);
    tick(1599U);
    RemoteService_ExecutionFault(); /* 重复故障不得续期。 */
    assert(!enabled[1] && !left_target && !right_target);
    assert(RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched && mock_power);
    tick(1600U);
    assert(RemoteService_State()->fault_latched && !mock_power);
    assert(!PatrolRuntime_ResetFault()); /* 转向未确认，不能以清锁绕过。 */
    unsigned before = sent_count;
    tick(1660U); tick(1700U);
    assert(sent_count > before);
    drop_node = 0U;
    for (uint32_t t = 1710U; t <= 1850U; t += 10U) tick(t);
    assert(PatrolRuntime_ResetFault());
    puts("PASS missing steering does not block drive; 599/600ms boundary; retry after latch");
}

// shutdown 写 ACK 丢失但状态读取可用时，确认真实关闭即可完成，不重放任何运动写入。
static void test_lost_shutdown_ack(void)
{
    fixture(); drop_shutdown_ack = 1U;
    RemoteService_ExecutionFault(); respond();
    for (uint32_t t = 1010U; t <= 1250U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !RemoteService_State()->fault_pending && !mock_power);
    assert(!enabled[0] && !enabled[1]);
    assert(PatrolRuntime_ResetFault());
    puts("PASS missing shutdown ACK recovered by status confirmation");
}

// 活动 Bus-Off 无法发帧时立即走锁存/下电退路；恢复 CAN 后，即使软件输出已经关闭，仍能发送安全撤使能。
static void test_bus_off_and_manual_power(void)
{
    fixture(); mock_bus_off = 1U;
    RemoteService_ExecutionFault(); tick(1000U);
    assert(!sent_count && RemoteService_State()->fault_latched && !mock_power);
    mock_bus_off = 0U;
    for (uint32_t t = 1010U; t <= 1120U; t += 10U) tick(t);
    assert(sent_count && !enabled[0] && !enabled[1]);
    assert(!sent[0].power && !sent[1].power);
    assert(PatrolRuntime_ResetFault());
    fixture(); mock_power = 0U;
    RemoteService_ExecutionFault(); respond();
    for (uint32_t t = 1010U; t <= 1100U; t += 10U) tick(t);
    assert(!enabled[0] && !enabled[1] && RemoteService_State()->fault_latched);
    puts("PASS Bus-Off fallback and powered-off CAN stop path");
}

// 验证生产转向事务在 199 ms 仍等待、200 ms 超时后优先发送双节点撤使能。
static void test_original_sdo_timeout(void)
{
    fixture();
    g_motion.bench.step = 30U; g_motion.bench.pending = 1U;
    g_motion.bench.index = 0x6081U; g_motion.bench.read = 1U; g_motion.bench.width = 4U;
    g_motion.bench.sent_ms = 801U;
    PatrolRuntime_Tick();
    assert(!sent_count && !RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched);
    assert(g_motion.bench.pending && !g_motion.bench.fault);
    mock_now = 1001U;
    PatrolRuntime_Tick();
    assert(sent_count == 2U && RemoteService_State()->fault_pending);
    assert(g_motion.bench.fault == 6U);
    respond();
    for (uint32_t t = 1010U; t <= 1100U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !mock_power);
    puts("PASS 199/200ms steering timeout boundary and both shutdowns before latch");
}

// 发送端拒绝提交、邮箱只剩一个、以及计时器回绕均不能阻塞主循环或提前解除故障禁止。
static void test_submission_and_wrap(void)
{
    fixture(); refuse_send = 1U;
    RemoteService_ExecutionFault();
    assert(!sent_count && RemoteService_State()->fault_pending);
    refuse_send = 0U;
    for (uint32_t t = 1010U; t <= 1120U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !enabled[0] && !enabled[1]);
    fixture(); capacity = 1U;
    RemoteService_ExecutionFault();
    assert(sent_count == 1U && sent[0].frame.id == 0x602U);
    tick(1010U);
    assert(sent[1].frame.id == 0x601U);
    PatrolFaultStop s; PatrolCanFrame frame;
    PatrolFaultStop_Init(&s); PatrolFaultStop_Begin(&s,3U,UINT32_MAX-100U);
    PatrolFaultStop_Tick(&s,498U); assert(!s.latch_due);
    PatrolFaultStop_Tick(&s,499U); assert(s.latch_due);
    assert(PatrolFaultStop_Take(&s,499U,&frame)); assert_safe_frame(&frame);
    PatrolFaultStop_Init(&s); PatrolFaultStop_Begin(&s,3U,65000U);
    unsigned requests = 0U;
    for (uint32_t t = 65000U; t < 71000U; t += 10U) {
        PatrolFaultStop_Tick(&s,t);
        for (unsigned j = 0U; j < 2U && PatrolFaultStop_Take(&s,t,&frame); ++j) {
            assert_safe_frame(&frame); ++requests;
        }
    }
    assert(requests > 100U && s.active && s.latch_due); /* 跨 16 位局部计时回绕仍持续安全重试。 */
    puts("PASS rejected submission, one mailbox, monotonic wrap");
}

// 写 ACK 和旧诊断缓存不能冒充撤使能完成；状态始终为 enabled 的节点必须进入未确认退路并继续重试。
static void test_ack_is_not_disabled(void)
{
    fixture(); ignore_shutdown_node = 1U;
    RemoteService_ExecutionFault();
    PatrolCanFrame old = PatrolCanopen_SdoRead(1U,0x6041U,0U);
    old.id = 0x581U; old.dlc = 6U; old.data[0] = 0x4BU; old.data[4] = 0x21U;
    PatrolRuntime_OnCanFrame(&old,mock_now); /* 本流程尚未发状态读取。 */
    assert(RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched);
    respond();
    for (uint32_t t = 1010U; t <= 1100U; t += 10U) tick(t);
    assert(enabled[0] && RemoteService_State()->fault_pending);
    for (uint32_t t = 1110U; t <= 1600U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !mock_power && !PatrolRuntime_ResetFault());
    ignore_shutdown_node = 0U;
    for (uint32_t t = 1610U; t <= 1740U; t += 10U) tick(t);
    assert(PatrolRuntime_ResetFault());
    puts("PASS write ACK and old status cannot fake disabled; stuck-enable fallback");
}

// 控制层换向超时和编码器门禁同样走生产停机回调，而非只覆盖执行器的 ApplyCut。
static void test_control_fault_sources(void)
{
    fixture();
    mock_now = 1001U;
    assert(RemoteService_ApplyLocal("renew","fault-test",2U,1000U,-1000,0,mock_now));
    assert(RemoteService_State()->reversal_pending);
    mock_now = 7000U;
    ChassisFeedback f = RemoteService_State()->feedback;
    f.feedback_ms = mock_now; f.left_actual_rpm = f.right_actual_rpm = 473;
    RemoteService_UpdateFeedback(&f);
    assert(RemoteService_ApplyLocal("renew","fault-test",3U,1000U,-1000,0,mock_now));
    mock_now = 7001U; f.feedback_ms = mock_now; RemoteService_UpdateFeedback(&f);
    RemoteService_SafetyTick(mock_now);
    assert(RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched);
    assert(RemoteService_State()->stop_reason == CHASSIS_STOP_DIRECTION_TIMEOUT && sent_count == 2U);
    respond();
    for (uint32_t t = 7010U; t <= 7100U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !mock_power);
    fixture();
    f = RemoteService_State()->feedback; f.encoder_range_valid = 0U;
    RemoteService_UpdateFeedback(&f); RemoteService_SafetyTick(mock_now);
    assert(RemoteService_State()->fault_pending && sent_count == 2U);
    assert(RemoteService_State()->stop_reason == CHASSIS_STOP_ENCODER_RANGE);
    puts("PASS reversal-timeout and encoder-range faults disable before latch");
}

// 高优先级安全任务可在普通目标选帧后启动故障停机；恢复原调用栈时不能把旧的非零速度发出去。
static void test_selected_motion_frame_revoked(void)
{
    fixture();
    g_motion.drive.step = 13U;
    g_motion.bench_turn = g_motion.drive_turn = 1U;
    inject_fault = 1U;
    PatrolRuntime_Tick();
    assert(!inject_fault && sent_count == 2U && RemoteService_State()->fault_pending);
    for (unsigned i = 0; i < sent_count; ++i) assert_safe_frame(&sent[i].frame);
    puts("PASS fault between take/send discards old nonzero motion frame");
}

// shutdown ACK 先于内部状态完成时，仅继续查询状态，避免在正常减速期间反复写 shutdown。
static void test_delayed_disable_after_ack(void)
{
    fixture(); delayed_shutdown_until = 1300U;
    RemoteService_ExecutionFault(); respond();
    for (uint32_t t = 1010U; t <= 1250U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_pending && !RemoteService_State()->fault_latched);
    unsigned shutdowns = 0U;
    for (unsigned i = 0U; i < sent_count; ++i)
        if (sent[i].frame.id == 0x601U && sent[i].frame.data[1] == 0x40U) ++shutdowns;
    assert(shutdowns == 1U);
    for (uint32_t t = 1260U; t <= 1370U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !enabled[0] && !mock_power);
    puts("PASS delayed disable: wait for real status without repeating acknowledged shutdown");
}

// 服务重新初始化不能取消尚未完成的独立停机，也不能通过它恢复运动或给故障续期。
static void test_service_reinit_during_stop(void)
{
    fixture(); RemoteService_ExecutionFault(); respond();
    RemoteService_Init();
    assert(!RemoteService_ApplyLocal("acquire","replacement",1U,1000U,0,0,mock_now));
    tick(1010U);
    assert(RemoteService_State()->fault_pending && aborts == 1U);
    for (uint32_t t = 1020U; t <= 1120U; t += 10U) tick(t);
    assert(RemoteService_State()->fault_latched && !mock_power);
    puts("PASS service reinit cannot cancel pending disable");
}

// 顺序执行独立安全场景，各场景都从全新主控和驱动器模拟状态开始。
int main(void)
{
    test_confirm_before_latch();
    test_late_abort_retries_same_safe_write();
    test_missing_node_and_deadline();
    test_lost_shutdown_ack();
    test_bus_off_and_manual_power();
    test_original_sdo_timeout();
    test_submission_and_wrap();
    test_ack_is_not_disabled();
    test_control_fault_sources();
    test_selected_motion_frame_revoked();
    test_delayed_disable_after_ack();
    test_service_reinit_during_stop();
    return 0;
}
