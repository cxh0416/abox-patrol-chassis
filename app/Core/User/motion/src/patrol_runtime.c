#include "motion_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_log.h"
#include "remote_service.h"
#include "patrol_config.h"
#include "patrol_can_status.h"
#include "patrol_can_remote.h"
#include "vehicle_power_output.h"
#include "vehicle_steering_profile.h"
#include "patrol_fault_stop.h"

PatrolMotion g_motion;

#ifndef PATROL_STEERING_EXECUTOR_ENABLED
#define PATROL_STEERING_EXECUTOR_ENABLED 0
#endif
#ifndef PATROL_DRIVE_EXECUTOR_ENABLED
#define PATROL_DRIVE_EXECUTOR_ENABLED 0
#endif
#ifndef PATROL_CHASSIS_ACTUATION_ENABLED
#define PATROL_CHASSIS_ACTUATION_ENABLED 0
#endif

static MotionFaultRecord last_fault;
static uint8_t fault_captured, fault_dirty, fault_storage;
static PatrolFaultStop fault_stop;
// 复制最近一次运动故障记录。在临界区读取固定快照，记录是否存在由返回值表示；故障记录与实时反馈是不同证据。
uint8_t PatrolRuntime_GetFault(MotionFaultRecord* record)
{
    PatrolRuntimePort_Enter(); *record = last_fault; PatrolRuntimePort_Exit();
    return record->magic == 0x4D463031U;
}
// 开启一个新的故障记录周期。仅重置本轮首故障捕获标志，后续首因由 CaptureFault 保存，不等同于清除驱动器故障。
void PatrolRuntime_BeginFaultEpisode(void) { if (!fault_dirty) fault_captured=0; }
// 读取运动故障记录的持久化状态码。返回当前存储标记，区分待处理、写入成功与写入失败，不能据此认定车辆已停车。
uint8_t PatrolRuntime_FaultStorage(void) { return fault_storage; }
// 在本轮首次故障时冻结运行上下文。记录来源、原因、门禁、双轮反馈和执行器状态并标记待存储；Flash 写入延后到断开输出且 OTA 不忙时。
void PatrolRuntime_CaptureFault(uint32_t source, uint32_t reason, uint32_t gates)
{
    PatrolRuntimePort_Enter();
    if (!fault_captured) {
        uint32_t now=PatrolRuntimePort_Now(), sequence=last_fault.sequence+1U;
        const ChassisControl* c=RemoteService_State();
        PatrolDiagnostics* d=&g_motion.diagnostics;
        memset(&last_fault,0,sizeof(last_fault));
        last_fault.magic=0x4D463031U; last_fault.sequence=sequence;
        last_fault.at_ms=now; last_fault.source=source; last_fault.reason=reason; last_fault.gates=gates;
        last_fault.target_rpm=g_motion.drive.phase != BENCH_IDLE ? g_motion.drive.writing : c->target_rpm; last_fault.target_mdeg=c->target_steering_mdeg;
        last_fault.left_rpm=d->left_actual_rpm; last_fault.right_rpm=d->right_actual_rpm;
        last_fault.driver_raw=d->steering_position_count; last_fault.encoder_raw=d->encoder_count;
        last_fault.steer_sw=d->steering_status_word; last_fault.drive_sw=d->drive_status_word;
        last_fault.left_code=(uint32_t)d->drive_objects[0]; last_fault.right_code=(uint32_t)d->drive_objects[1];
        last_fault.left_code_age=d->drive_code_ms[0] ? now-d->drive_code_ms[0] : UINT32_MAX;
        last_fault.right_code_age=d->drive_code_ms[1] ? now-d->drive_code_ms[1] : UINT32_MAX;
        last_fault.steer_age=d->steering_status_ms ? now-d->steering_status_ms : UINT32_MAX;
        last_fault.drive_age=d->drive_status_ms ? now-d->drive_status_ms : UINT32_MAX;
        last_fault.bench_phase=g_motion.bench.phase; last_fault.bench_step=g_motion.bench.step;
        last_fault.bench_fault=g_motion.bench.fault; last_fault.drive_phase=g_motion.drive.phase;
        last_fault.drive_step=g_motion.drive.step; last_fault.drive_fault=g_motion.drive.fault;
        last_fault.last_abort=d->last_sdo_abort_code; last_fault.encoder_error=d->last_encoder_error;
        last_fault.center_phase=g_motion.center_cal.phase;
        fault_captured=1; fault_dirty=1; fault_storage=0;
    }
    PatrolRuntimePort_Exit();
}

// 每次最多提交两条安全帧，使两个节点的 shutdown 均能优先进入邮箱；不等待某个节点 ACK，不发送普通诊断或运动命令。
static void dispatch_fault_stop(uint32_t now)
{
    if (PatrolRuntimePort_CanBusOffActive()) return;
    for (uint8_t count = 0U; count < 2U && PatrolRuntimePort_CanTxReady(); ++count) {
        PatrolCanFrame frame;
        if (!PatrolFaultStop_Take(&fault_stop, now, &frame)) break;
        if (!PatrolRuntimePort_SendCan(&frame))
            PatrolFaultStop_TxFailed(&fault_stop, (uint8_t)(frame.id - 0x600U), now);
    }
}

// 在控制层正式锁存前接管 CAN 停机。冻结首因、撤销授权并中止旧目标，独立发送 6040=6；此处不依赖软件 OUT1 为开。
uint8_t PatrolRuntime_RequestFaultStop(void)
{
    const uint8_t nodes = PATROL_CHASSIS_ACTUATION_ENABLED ?
        (PATROL_STEERING_EXECUTOR_ENABLED ? PATROL_FAULT_STOP_STEERING : 0U) |
        (PATROL_DRIVE_EXECUTOR_ENABLED ? PATROL_FAULT_STOP_DRIVE : 0U) : 0U;
    if (!nodes) return 0U;
    PatrolRuntimePort_Enter();
    uint32_t now = PatrolRuntimePort_Now();
    if (!fault_stop.active && !RemoteService_State()->fault_latched) {
        PatrolFaultStop_Init(&fault_stop);
        PatrolRuntime_CaptureFault(1U, (uint32_t)RemoteService_State()->stop_reason, g_motion.feedback_gate_mask);
        RemoteService_Revoke();
        PatrolAutoCenter_Cancel(&g_motion.auto_center);
        PatrolRuntimePort_AbortCanTx();
        g_motion.diagnostics.pending_valid = 0U;
        BenchDrive_Stop(&g_motion.drive, now, 1U);
        BenchSteering_Stop(&g_motion.bench, now, 1U);
        g_motion.drive.pending = g_motion.bench.pending = 0U;
        g_motion.drive.cut_power = g_motion.bench.cut_power = 0U;
        g_motion.bench_transport_stopped = 1U;
        PatrolFaultStop_Begin(&fault_stop, nodes, now);
        dispatch_fault_stop(now);
    }
    uint8_t pending = fault_stop.active;
    PatrolRuntimePort_Exit();
    return pending;
}

// 优先推进独立故障停机。确认关闭后再锁存和下电；600 ms 未确认或活动 Bus-Off 时先锁存/下电，仍保留仅撤使能的重试。
static uint8_t service_fault_stop(void)
{
    PatrolRuntimePort_Enter();
    const ChassisControl* control = RemoteService_State();
    if (!fault_stop.active && !control->fault_pending) { PatrolRuntimePort_Exit(); return 0U; }
    /* 配置侧重新初始化服务也不能解除尚未完成的撤使能；恢复禁止标志但不重置停机起点。 */
    if (fault_stop.active && !control->fault_pending && !control->fault_latched)
        RemoteService_ExecutionFault();
    uint32_t now = PatrolRuntimePort_Now();
    PatrolFaultStop_Tick(&fault_stop, now);
    if (PatrolRuntimePort_CanBusOffActive()) fault_stop.latch_due = 1U;
    dispatch_fault_stop(now);
    if (!fault_stop.active || fault_stop.latch_due) {
        if (control->fault_pending) {
            APP_LOGW("FAULTSTOP", "disable mask=%u/%u confirmed=%u; latch",
                (unsigned)fault_stop.disabled_mask, (unsigned)fault_stop.required_mask,
                (unsigned)!fault_stop.active);
            RemoteService_CompleteFaultStop();
        }
        (void)VehiclePowerOutput_Set(0U);
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.bench.limits_aligned = 0U;
    }
    g_motion.drive.cut_power = g_motion.bench.cut_power = 0U;
    PatrolRuntimePort_Exit();
    /* 故障快照先于停机冻结；Flash 仍只允许在输出关闭及 OTA 空闲时写入。 */
    if (fault_dirty && !VehiclePowerOutput_IsOn() && !PatrolRuntimePort_OtaBusy()) {
        fault_storage = OTA_Info_SaveMotionFault(&last_fault) ? 1U : 2U;
        fault_dirty = 0U;
    }
    return 1U;
}

// 初始化运动诊断、执行器与标定运行上下文。载入既有故障证据并建立安全默认状态，不自动恢复遥控会话或发出运动目标。
void PatrolRuntime_Init(void)
{
    PatrolFaultStop_Init(&fault_stop);
    PatrolDiagnostics_Init(&g_motion.diagnostics);
    PatrolAutoCenter_Cancel(&g_motion.auto_center);
    memset(&last_fault,0,sizeof(last_fault));
    fault_captured=0; fault_dirty=0;
    fault_storage=OTA_Info_LoadMotionFault(&last_fault) ? 1U : 0U;
}

// 取得带当前时间判断的运动诊断快照。在运行端口临界区内统一取样，供业务查询使用；调用者不应把旧快照当作当前运动许可。
void PatrolRuntime_GetDiagnostics(PatrolDiagnosticsSnapshot* snapshot)
{
    if (snapshot == NULL) return;
    PatrolRuntimePort_Enter();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, PatrolRuntimePort_Now(), snapshot);
    PatrolRuntimePort_Exit();
}

// 在安全前提满足时申请清除 MCU 运动故障。复位路径检查输出、OTA、标定与执行器上下文；这是软件故障复位入口，不保证外部驱动器故障已消失。
uint8_t PatrolRuntime_ResetFault(void)
{
    PatrolRuntimePort_Enter();
    if (VehiclePowerOutput_IsOn() || PatrolRuntimePort_OtaBusy() || fault_dirty || fault_stop.active ||
        RemoteService_State()->fault_pending ||
        g_motion.center_cal.encoder_write_state==4U) { PatrolRuntimePort_Exit(); return 0; }
    RemoteService_Revoke(); PatrolRuntimePort_AbortCanTx();
    BenchSteering_Init(&g_motion.bench); BenchDrive_Init(&g_motion.drive);
    g_motion.diagnostics.pending_valid=0; g_motion.bench_transport_stopped=0;
    SteeringCoordinate_Reset(&g_motion.coordinate); g_motion.coordinate_reset_requested=0;
    PatrolAutoCenter_Cancel(&g_motion.auto_center);
    g_motion.center_cal.phase=CENTER_CAL_IDLE; g_motion.center_cal.session_id[0]=0;
    g_motion.center_cal.encoder_write_state=0; g_motion.center_cal.fault=0;
    RemoteService_Init(); /* No permit, enable, target or previous session survives. */
    fault_captured=0;
    PatrolFaultStop_Init(&fault_stop);
    PatrolRuntimePort_Exit(); return 1;
}

// 将驱动器 CAN 反馈分发给诊断与执行器。同一帧可用于状态维护和在途响应匹配；调用方须按运行层并发约定保护共享状态。
void PatrolRuntime_OnCanFrame(const PatrolCanFrame* frame, uint32_t now)
{
    PatrolRuntimePort_Enter();
    /* The steering driver broadcasts its validated status/position mapping.
     * Consume those TPDOs independently of SDO ownership and stop arbitration. */
    uint8_t steering_tpdo = PatrolDiagnostics_OnTpdoFrame(&g_motion.diagnostics, frame, now);
    if (PatrolFaultStop_OnFrame(&fault_stop, frame, now)) {
        /* 原诊断事务已经取消；本流程的状态响应仍更新现场诊断时间和状态字。 */
        PatrolDiagnostics_OnCanFrame(&g_motion.diagnostics, frame, now);
        int32_t status; uint8_t width;
        uint8_t node = (uint8_t)(frame->id - 0x580U);
        if (PatrolCanopen_ParseSdoValue(frame, node, 0x6041U, 0U, &status, &width) && width == 2U) {
            if (node == 1U) { g_motion.diagnostics.steering_status_word = (uint16_t)status; g_motion.diagnostics.steering_status_ms = now; }
            else { g_motion.diagnostics.drive_status_word = (uint16_t)status; g_motion.diagnostics.drive_status_ms = now; }
        }
        PatrolRuntimePort_Exit();
        return;
    }
    /* Exactly one SDO owner; executor responses never satisfy a diagnostic request. */
    if ((frame->id == 0x701 || frame->id == 0x702) && frame->dlc == 1 && frame->data[0] == 0 &&
        (g_motion.coordinate.valid || CenterCalibration_IsActive()))
        g_motion.coordinate_reset_requested = 1;
    uint8_t execution = BenchSteering_OnFrame(&g_motion.bench, frame, now) ? 1 :
        BenchDrive_OnFrame(&g_motion.drive, frame, now) ? 2 : 0;
    if (!execution && !steering_tpdo)
        PatrolDiagnostics_OnCanFrame(&g_motion.diagnostics, frame, now);
    else if (steering_tpdo)
        g_motion.diagnostics.can_rx_count++;
    else {
        int32_t status; uint8_t width;
        g_motion.diagnostics.can_rx_count++;
        /* An executor-owned validated status upload is also fresh feedback;
         * it must not complete a diagnostics transaction or advance its cursor. */
        if (PatrolCanopen_ParseSdoValue(frame, execution, 0x6041, 0, &status, &width) && width == 2) {
            if (execution == 1) {
                g_motion.diagnostics.steering_status_word = (uint16_t)status;
                g_motion.diagnostics.steering_status_ms = now;
            } else {
                g_motion.diagnostics.drive_status_word = (uint16_t)status;
                g_motion.diagnostics.drive_status_ms = now;
            }
        }
    }
    PatrolRuntimePort_Exit();
}

// 处理下电或节点复位引起的转向坐标失效。下电清回中和限位对齐缓存，带电坐标周期丢失则停止转向并按标定阶段保留故障；调用方按运行层约定持锁。
void PatrolRuntime_ApplyReset(uint32_t now)
{
    if (!VehiclePowerOutput_IsOn()) {
        PatrolAutoCenter_Cancel(&g_motion.auto_center);
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.bench.limits_aligned = 0;
    }
    if (g_motion.coordinate_reset_requested) {
        /* Power-off already invalidated the coordinate above.  A late
         * node Boot-up heartbeat during intentional de-energization
         * must not turn an idle executor into BENCH_FAULT.  While
         * powered, a reset of a valid coordinate remains a latched
         * safety fault. */
        if (VehiclePowerOutput_IsOn() && CenterCalibration_IsActive()) {
            PatrolDiagnosticsSnapshot reset_snapshot;
            PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &reset_snapshot);
            CenterCalibration_Fail(2U, CENTER_CAL_REASON_POSITION_EPOCH_RESET,
                &reset_snapshot, CenterCalibration_GateMask(&reset_snapshot, 0U));
        }
        if (g_motion.coordinate.valid) BenchSteering_Stop(&g_motion.bench, now, 1);
        SteeringCoordinate_Reset(&g_motion.coordinate);
        g_motion.coordinate_reset_requested = 0;
    }
}

// 处理执行器请求的切电及故障传播。将执行错误汇总到控制层并更新动力输出命令；返回处理后的时间基准，输出写入成功不代表已获得接触器物理反馈。
uint32_t PatrolRuntime_ApplyCut(uint32_t now)
{
    if (g_motion.bench.cut_power || g_motion.drive.cut_power) {
        PatrolRuntimePort_Enter();
        now = PatrolRuntimePort_Now();
        ChassisStopReason reason = RemoteService_State()->stop_reason;
        PatrolRuntime_CaptureFault(1U, (uint32_t)(reason == CHASSIS_STOP_NONE ?
            CHASSIS_STOP_EXECUTION_FAULT : reason), g_motion.feedback_gate_mask);
        /* Capture before cross-stopping the other executor and before
         * power-off turns healthy samples into secondary offline gates. */
        if (CenterCalibration_IsActive()) {
            PatrolDiagnosticsSnapshot fault_snapshot;
            PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &fault_snapshot);
            CenterCalibration_CaptureFailureLocked(CENTER_CAL_REASON_BENCH_EXECUTOR_FAULT,
                CenterCalibration_GateMask(&fault_snapshot, 0U), now, &fault_snapshot);
            g_motion.center_cal.phase = CENTER_CAL_FAILED;
            g_motion.center_cal.fault = 2U;
            g_motion.center_cal.encoder_write_state = 0U;
        }
        BenchDrive_Stop(&g_motion.drive, now, 1);
        BenchSteering_Stop(&g_motion.bench, now, 1);
        g_motion.bench_fault_gate_mask = g_motion.feedback_gate_mask;
        RemoteService_ExecutionFault();
        if (!fault_stop.active && !RemoteService_State()->fault_pending) (void)VehiclePowerOutput_Set(0);
        g_motion.bench.cut_power = 0; g_motion.drive.cut_power = 0;
        PatrolRuntimePort_Exit();
    }
    return now;
}

// 在执行器和只读诊断之间仲裁下一个 CAN 请求。通过 owner 标识报文归属，维持运动期间事务优先级与交替读取；返回报文仅表示选中，真正发送前仍须复核授权。
uint8_t PatrolRuntime_Take(uint32_t now, PatrolCanFrame* request, uint8_t* owner)
{
    uint8_t have_request = 0, bench_frame = 0, allow_retry;
    /* While a motion write has an unknown outcome, only its bounded uploads
     * may progress. Do not send either axis' queued movement in the meantime. */
    if (g_motion.bench.verify || g_motion.drive.verify) {
        if (g_motion.diagnostics.pending_valid || g_motion.bench.pending || g_motion.drive.pending) return 0U;
        *owner = g_motion.bench.verify ? 1U : 2U;
        return *owner == 1U ? BenchSteering_Take(&g_motion.bench, now, request) :
            BenchDrive_Take(&g_motion.drive, now, request);
    }
    /* Reserve the bus for feedback that is approaching its 250 ms expiry.
     * Steering position updates can otherwise keep issuing PP target writes
     * while a Node 2 status upload is still due. */
    if (!g_motion.bench.pending && !g_motion.drive.pending &&
        g_motion.bench.phase != BENCH_STOPPING &&
        g_motion.drive.phase != BENCH_STOPPING &&
        PatrolDiagnostics_HasUrgentFeedback(&g_motion.diagnostics, now)) {
        const ChassisControl* control = RemoteService_State();
        allow_retry = ChassisControl_SafetyReady(control, now) &&
            VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
            !PatrolRuntimePort_OtaBusy();
        have_request = PatrolDiagnostics_TakeSdoReadUrgent(
            &g_motion.diagnostics, now, request, allow_retry);
        if (have_request) g_motion.bench_turn = 1U;
        *owner = 0U;
        /*
         * If an existing low-priority SDO has not yet reached its bounded
         * timeout, keep the actuator writes off the bus until the urgent
         * read can be selected.
         */
        return have_request;
    }
    /* Alternate execution and diagnostics, never overlap pending transactions. */
    g_motion.diagnostics.stop_priority = g_motion.bench.phase == BENCH_STOPPING;
    if (!g_motion.diagnostics.pending_valid && !g_motion.bench.pending && !g_motion.drive.pending &&
        (g_motion.bench_turn || BenchSteering_StopFinishReady(&g_motion.bench))) {
        if (g_motion.drive_turn && g_motion.bench.phase != BENCH_STOPPING) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
        if (!have_request) {
            have_request = BenchSteering_Take(&g_motion.bench, now, request);
            if (have_request) { bench_frame = 1; g_motion.drive_turn = 1; }
        }
        if (!have_request) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
        if (have_request) g_motion.bench_turn = 0;
    }
    if (!have_request && !g_motion.bench.pending && !g_motion.drive.pending) {
        const ChassisControl* control = RemoteService_State();
        allow_retry = ChassisControl_SafetyReady(control, now) &&
            VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
            !PatrolRuntimePort_OtaBusy();
        uint32_t before_timeouts = g_motion.diagnostics.sdo_timeout_count;
        have_request = PatrolDiagnostics_TakeSdoRead(&g_motion.diagnostics, now, request, allow_retry);
        if (g_motion.diagnostics.sdo_timeout_count != before_timeouts)
            APP_LOGW("SDOREAD", "timeout entry=%u retry=%u pending=%u total=%lu",
                (unsigned)g_motion.diagnostics.pending_entry, (unsigned)g_motion.diagnostics.pending_retry,
                (unsigned)g_motion.diagnostics.pending_valid, (unsigned long)g_motion.diagnostics.sdo_timeout_count);
        if (have_request) g_motion.bench_turn = 1;
    }
    if (!have_request && !g_motion.diagnostics.pending_valid && !g_motion.bench.pending && !g_motion.drive.pending) {
        if (g_motion.drive_turn && g_motion.bench.phase != BENCH_STOPPING) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
        if (!have_request) {
            have_request = BenchSteering_Take(&g_motion.bench, now, request);
            if (have_request) { bench_frame = 1; g_motion.drive_turn = 1; }
        }
        if (!have_request) {
            have_request = BenchDrive_Take(&g_motion.drive, now, request);
            if (have_request) { bench_frame = 2; g_motion.drive_turn = 0; }
        }
    }
    *owner = bench_frame;
    return have_request;
}

// Stop responsibility follows executor state; session ownership only grants new targets.
enum { FEEDBACK_STOP_STEERING = 1U, FEEDBACK_STOP_DRIVE = 2U };

static uint8_t executor_requires_stop(BenchPhase phase)
{
    return (phase == BENCH_PREPARING || phase == BENCH_ACTIVE || phase == BENCH_STOPPING) ? 1U : 0U;
}

static uint32_t feedback_due;

/* >>>>> DRIVE-RECOVERY: START >>>>> */
/* A hot MCU reset leaves the drive controllers in their previous states, which
 * can still be Operation Enabled. The normal chain then deadlocks: the steering
 * coordinate and power_ready require the steering disabled and the drive not
 * enabled, but the executors that would disable them cannot start until
 * power_ready. Break the deadlock by forcing both nodes pre-operational when
 * they are enabled, nothing owns the actuators and both executors are idle. */
// 判断是否需要补发驱动器 shutdown 恢复命令。以会话、执行器、供电和恢复节流状态约束补发，避免持续停留在 Operation Enabled；不能取代正常停车确认。
static uint8_t recovery_wanted(uint32_t now)
{
    const ChassisControl* c = RemoteService_State();
    if (g_motion.bench.phase != BENCH_IDLE || g_motion.drive.phase != BENCH_IDLE) return 0U;
    if (c->session_active || g_motion.center_cal.phase != CENTER_CAL_IDLE) return 0U;
    if ((g_motion.diagnostics.steering_status_word & 0x6FU) == 0x27U &&
        (uint32_t)(now - g_motion.diagnostics.steering_status_ms) <= 250U) return 1U;
    if ((g_motion.diagnostics.drive_status_word & 0x4FU) == 7U &&
        (uint32_t)(now - g_motion.diagnostics.drive_status_ms) <= 250U) return 1U;
    return 0U;
}
/* <<<<< DRIVE-RECOVERY: END <<<<< */

// 查询本地自动回中是否持有转向请求。供业务状态和任务协调使用；不把请求存在等同于回中已经完成。
uint8_t PatrolRuntime_AutoCenterActive(void)
{
    uint8_t active;
    PatrolRuntimePort_Enter(); active = g_motion.auto_center.requested; PatrolRuntimePort_Exit();
    return active;
}

// 计算两个新鲜样本的最短时间差，兼容毫秒计数自然回绕。
static uint32_t angle_sample_skew(uint32_t first, uint32_t second)
{
    uint32_t forward = first - second;
    uint32_t backward = second - first;
    return forward < backward ? forward : backward;
}

// 检查无遥控会话时本地自动回中可用条件。双角差只在启动前核对，执行中继续监督其他安全门禁。
static uint8_t auto_center_safe(const ChassisControl* control, uint32_t now)
{
    return PATROL_CHASSIS_ACTUATION_ENABLED && PATROL_STEERING_EXECUTOR_ENABLED &&
        !control->auto_center_inhibited &&
        !CenterCalibration_IsActive() && g_motion.coordinate.valid && VehiclePowerOutput_IsOn() &&
        !PatrolRuntimePort_CanBusOffActive() && !PatrolRuntimePort_OtaBusy() &&
        ChassisControl_SafetyReady(control, now);
}

// 汇总驱动器与绝对编码器反馈并更新安全仲裁输入。同锁读取时间和诊断快照，建立运行坐标并归一化镜像右轮方向；标定相对反馈、Bus-Off 恢复观察和反馈门禁在此同步更新。
void PatrolRuntime_UpdateFeedback(uint32_t now)
{
    PatrolDiagnosticsSnapshot snapshot;
    ChassisFeedback feedback;
    const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
    memset(&feedback, 0, sizeof(feedback));
    PatrolRuntimePort_Enter();
    /* Read time and feedback under the same lock: RX can otherwise publish a
     * timestamp newer than now and unsigned age arithmetic reports it stale. */
    now = PatrolRuntimePort_Now();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, now, &snapshot);
    PatrolRuntimePort_Exit();
    /* Clear only the recovered CAN gate. Sessions remain revoked. */
    PatrolRtos_ObserveCanRecovery(snapshot.coherent_feedback_ms,
        VehiclePowerOutput_IsOn() && snapshot.steering_online && snapshot.drive_online &&
        snapshot.steering_feedback_fresh && snapshot.drive_feedback_fresh &&
        snapshot.encoder_online && snapshot.encoder_config_valid && !snapshot.drive_fault &&
        labs(snapshot.left_actual_rpm) <= 20L && labs(snapshot.right_actual_rpm) <= 20L);
    feedback.calibrated = cfg != NULL && cfg->calibrated && PatrolConfig_CalibrationCompatible();
    feedback.encoder_profile_ready = PatrolConfig_SingleTurnProfileReady();
    feedback.steering_online = snapshot.steering_online;
    feedback.drive_online = snapshot.drive_online;
    feedback.can_bus_off = PatrolRuntimePort_CanBusOff();
    feedback.drive_fault = snapshot.drive_fault;
    feedback.ota_busy = PatrolRuntimePort_OtaBusy();
    /* Readiness uses driver and encoder evidence, never a nonexistent contactor input. */
    g_motion.feedback_gate_mask = (!snapshot.steering_feedback_fresh ? 1U : 0U) |
        (!snapshot.drive_feedback_fresh ? 2U : 0U) | (!snapshot.encoder_online ? 4U : 0U) |
        (!snapshot.steering_status_fresh ? 8U : 0U) | (!snapshot.drive_status_fresh ? 16U : 0U) |
        (!snapshot.encoder_config_valid ? 32U : 0U) |
        ((snapshot.drive_status_word & 4U) != 0 ? 64U : 0U) |
        ((labs(snapshot.left_actual_rpm) > 20 || labs(snapshot.right_actual_rpm) > 20) ? 128U : 0U) |
        (!VehiclePowerOutput_IsOn() ? 256U : 0U);
    /* An executing drive session may pass moving/Operation Enabled gates
     * while its bounded stop sequence is in progress. */
    if (g_motion.drive.phase != BENCH_IDLE && g_motion.drive.phase != BENCH_FAULT)
        g_motion.feedback_gate_mask &= (uint16_t)~(64U | 128U);
    feedback.encoder_range_valid = SteeringAngle_AbsoluteWithinLimit(
        snapshot.encoder_count, cfg->absolute_center_count,
        cfg->absolute_counts_per_degree_milli, cfg->absolute_direction,
        VEHICLE_STEERING_TRIP_MDEG, &feedback.encoder_actual_mdeg);
    if (PATROL_STEERING_EXECUTOR_ENABLED) {
        uint8_t was_valid = g_motion.coordinate.valid;
        if (!VehiclePowerOutput_IsOn() || CenterCalibration_IsActive()) SteeringCoordinate_Reset(&g_motion.coordinate);
        else if (g_motion.coordinate.valid && SteeringCoordinate_EpochLost(
            g_motion.feedback_gate_mask, snapshot.steering_online,
            cfg->steering_center_count == g_motion.coordinate_calibration_center &&
            cfg->absolute_center_count == g_motion.coordinate_absolute_center)) {
            /* A lost position epoch requires power-off, never a live re-zero. */
            PatrolRuntimePort_Enter();
            BenchSteering_Stop(&g_motion.bench, now, 1);
            SteeringCoordinate_Reset(&g_motion.coordinate);
            PatrolRuntimePort_Exit();
        } else {
            SteeringCoordinate_Observe(&g_motion.coordinate,
                g_motion.feedback_gate_mask == 0 && feedback.calibrated && feedback.encoder_range_valid &&
                snapshot.steering_online && snapshot.drive_online &&
                !snapshot.drive_fault && !feedback.can_bus_off && !feedback.ota_busy &&
                (snapshot.steering_status_word & 0x6FU) != 0x27U,
                snapshot.steering_position_count, snapshot.encoder_count, cfg->absolute_center_count,
                snapshot.coherent_feedback_ms, now);
        }
        if (!was_valid && g_motion.coordinate.valid) {
            g_motion.coordinate_calibration_center = cfg->steering_center_count;
            g_motion.coordinate_absolute_center = cfg->absolute_center_count;
        }
        if (!g_motion.coordinate.valid) g_motion.feedback_gate_mask |= 512U;
    }
    feedback.power_ready = PATROL_CHASSIS_ACTUATION_ENABLED &&
        PATROL_STEERING_EXECUTOR_ENABLED && g_motion.feedback_gate_mask == 0;
    feedback.left_actual_rpm = snapshot.left_actual_rpm > 32767 ? 32767 :
        (snapshot.left_actual_rpm < -32768 ? -32768 : (int16_t)snapshot.left_actual_rpm);
    /* Normalize mirror-mounted right-wheel feedback to vehicle coordinates.
     * Raw controller sign remains available in get_can_diag. */
    feedback.right_actual_rpm = snapshot.right_actual_rpm > 32768 ? -32768 :
        (snapshot.right_actual_rpm < -32767 ? 32767 : (int16_t)-snapshot.right_actual_rpm);
    feedback.angle_pair_valid = (uint8_t)(snapshot.steering_feedback_fresh && snapshot.encoder_online &&
        angle_sample_skew(snapshot.steering_position_ms, snapshot.encoder_sample_ms) <=
            CHASSIS_STEERING_PAIR_SKEW_MS);
    if (feedback.calibrated)
    {
        feedback.steering_actual_mdeg = SteeringAngle_FromControllerCount(
            snapshot.steering_position_count,
            PATROL_STEERING_EXECUTOR_ENABLED && g_motion.coordinate.valid ?
                g_motion.coordinate.runtime_center : cfg->steering_center_count,
            cfg->steering_counts_per_degree_milli, cfg->steering_direction);
        if (!feedback.encoder_range_valid)
            feedback.encoder_actual_mdeg = SteeringAngle_FromAbsoluteCount(
                snapshot.encoder_count, cfg->absolute_center_count,
                cfg->absolute_counts_per_degree_milli, cfg->absolute_direction);
    }
    feedback.feedback_ms = snapshot.coherent_feedback_ms != 0U ?
        snapshot.coherent_feedback_ms : now - (CHASSIS_FEEDBACK_TIMEOUT_MS + 1U);
    PatrolRuntimePort_Enter();
    CenterCalibration_UpdateRelative(&snapshot);
    RemoteService_UpdateFeedback(&feedback);
    if (g_motion.center_cal.phase == CENTER_CAL_JOGGING && g_motion.bench.phase == BENCH_ACTIVE &&
        snapshot.coherent_feedback_ms != 0U &&
        snapshot.coherent_feedback_ms != g_motion.center_cal.last_sample_ms &&
        g_motion.center_cal.relative_valid &&
        labs(g_motion.center_cal.driver_relative_mdeg - g_motion.center_cal.target_mdeg) <= 200L &&
        labs(g_motion.center_cal.encoder_relative_mdeg - g_motion.center_cal.target_mdeg) <= 200L) {
        g_motion.center_cal.last_sample_ms = snapshot.coherent_feedback_ms;
        if (++g_motion.center_cal.stable_samples >= 3U) {
            BenchSteering_Stop(&g_motion.bench, now, 0U);
            g_motion.center_cal.phase = CENTER_CAL_STOPPING;
            g_motion.center_cal.deadline_ms = now + 2000U;
        }
    }
    PatrolRuntimePort_Exit();
    feedback_due = now + 20U;
}

// 执行一次运动运行层的非阻塞调度。仲裁遥控、标定和回中，推进双执行器并处理切电、故障存储与 CAN 请求；发送前再次检查会话和租约，防止选帧后授权被撤销。
void PatrolRuntime_Tick(void)
{
    PatrolCanFrame request;
    uint8_t have_request = 0U;
    uint32_t now;
    uint8_t bench_frame = 0U;
    if (service_fault_stop()) return;
    PatrolRuntimePort_Enter();
    now = PatrolRuntimePort_Now();
    {
        const ChassisControl* c = RemoteService_State();
        const PatrolRemoteConfigV5_t* cfg = PatrolConfig_Get();
        uint8_t controlled_feedback_stop;
        PatrolRuntime_ApplyReset(now);
        uint8_t normal_safe = g_motion.coordinate.valid && VehiclePowerOutput_IsOn() &&
            !PatrolRuntimePort_CanBusOffActive() && ChassisControl_SafetyReady(c, now);
        uint8_t remote_requested = c->session_active && c->enable_request &&
            (int32_t)(now - c->lease_deadline_ms) < 0;
        /*
         * A single delayed status upload is a transport-scheduling event, not
         * proof that the drive actuator rejected a write. Stop through the
         * normal bounded zero/disable path and let a genuine stop failure
         * decide whether to latch. Stop responsibility follows executor state,
         * not session ownership: an explicit stop releases the session before
         * either executor has necessarily entered STOPPING. Node, bus and
         * drive faults retain the immediate fault path below.
         */
        uint8_t transient_feedback = (uint8_t)(
            (g_motion.feedback_gate_mask & (1U | 2U | 8U | 16U)) != 0U);
        uint8_t explicit_stop = (uint8_t)(
            c->stop_reason == CHASSIS_STOP_EXPLICIT);
        uint8_t feedback_stop_transport_ready = (uint8_t)(
            !CenterCalibration_IsActive() && VehiclePowerOutput_IsOn() &&
            !c->feedback.can_bus_off && !PatrolRuntimePort_CanBusOffActive() &&
            c->feedback.steering_online && c->feedback.drive_online &&
            !c->feedback.drive_fault);
        if (!executor_requires_stop(g_motion.bench.phase))
            g_motion.feedback_stop_mask &= (uint8_t)~FEEDBACK_STOP_STEERING;
        if (!executor_requires_stop(g_motion.drive.phase))
            g_motion.feedback_stop_mask &= (uint8_t)~FEEDBACK_STOP_DRIVE;
        if (CenterCalibration_IsActive())
            g_motion.feedback_stop_mask = 0U;
        else if ((transient_feedback || explicit_stop) && feedback_stop_transport_ready) {
            if (executor_requires_stop(g_motion.bench.phase))
                g_motion.feedback_stop_mask |= FEEDBACK_STOP_STEERING;
            if (executor_requires_stop(g_motion.drive.phase))
                g_motion.feedback_stop_mask |= FEEDBACK_STOP_DRIVE;
        }
        controlled_feedback_stop = (uint8_t)(
            !c->fault_latched && !c->fault_pending &&
            g_motion.feedback_stop_mask != 0U &&
            feedback_stop_transport_ready);
        if (controlled_feedback_stop) remote_requested = 0U;
        uint8_t center_requested = g_motion.center_cal.phase == CENTER_CAL_JOGGING;
        uint8_t center_executor_active = center_requested ||
            g_motion.center_cal.phase == CENTER_CAL_STOPPING;
        uint8_t center_safe = VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
            ChassisControl_RuntimeSafetyReason(c, now) == CHASSIS_STOP_NONE &&
            (g_motion.feedback_gate_mask & (uint16_t)~512U) == 0U && g_motion.center_cal.relative_valid &&
            llabs((int64_t)g_motion.center_cal.driver_relative_mdeg -
                  g_motion.center_cal.encoder_relative_mdeg) <= VEHICLE_STEERING_TOLERANCE_MDEG;
        uint8_t stop_transport_safe = VehiclePowerOutput_IsOn() &&
            !PatrolRuntimePort_CanBusOffActive() && !PatrolRuntimePort_OtaBusy();
        if (center_requested && !center_safe && stop_transport_safe) {
            CenterCalibration_RequestStopForFault(
                g_motion.center_cal.relative_valid ? CENTER_CAL_REASON_GATE_NOT_READY :
                CENTER_CAL_REASON_RELATIVE_MISMATCH, g_motion.feedback_gate_mask, now);
            center_requested = 0U;
            center_executor_active = 1U;
        }
        /* STOPPING still belongs to the commissioning executor.  Falling
         * back to normal_safe here rejects an intentionally uncalibrated
         * vehicle while the executor is sending its bounded stop sequence,
         * which turns a successful jog into BENCH_FAULT and cuts power. */
        uint8_t safe = center_executor_active ? center_safe : normal_safe;
        uint8_t local_safe = auto_center_safe(c, now);
        uint8_t local_moving = 0U;
        if (remote_requested || CenterCalibration_IsActive() || PatrolCanRemote_CalibrationPrepareActive())
            PatrolAutoCenter_Cancel(&g_motion.auto_center);
        else {
            /* A lost lease revokes remote targets. Local centering owns only
             * the steering axis and first waits for the drive's verified
             * zero-speed/disable sequence to finish. SAFETY_STOP stays parked. */
            if (!g_motion.auto_center.requested && !g_motion.auto_center.failed && local_safe &&
                (labs(c->feedback.steering_actual_mdeg) > PATROL_AUTO_CENTER_TOLERANCE_MDEG ||
                 labs(c->feedback.encoder_actual_mdeg) > PATROL_AUTO_CENTER_TOLERANCE_MDEG))
            {
                /* Wait for comparable samples before starting. A normal remote
                 * target never passes through this centering-only check. */
                if (c->feedback.angle_pair_valid &&
                    llabs((int64_t)c->feedback.steering_actual_mdeg -
                          c->feedback.encoder_actual_mdeg) > VEHICLE_STEERING_TOLERANCE_MDEG) {
                    RemoteService_SafetyFault(CHASSIS_STOP_ANGLE_MISMATCH);
                    normal_safe = safe = local_safe = 0U;
                } else if (c->feedback.angle_pair_valid) {
                    PatrolAutoCenter_Request(&g_motion.auto_center, now, c->feedback.steering_actual_mdeg);
                }
            }
            local_moving = PatrolAutoCenter_Tick(&g_motion.auto_center, now, local_safe,
                g_motion.drive.phase == BENCH_IDLE, &c->feedback);
            if (g_motion.auto_center.failed) {
                RemoteService_ExecutionFault();
                BenchSteering_Stop(&g_motion.bench, now, 1U);
            }
        }
        uint8_t local_requested = g_motion.auto_center.requested && local_safe;
        uint8_t requested = remote_requested || center_requested || local_requested;
        uint8_t executor_safe = (uint8_t)(safe || controlled_feedback_stop);
        uint8_t executor_requested = (uint8_t)(requested && !controlled_feedback_stop);
        int32_t requested_angle = center_requested ? g_motion.center_cal.target_mdeg :
            local_requested ? (local_moving ? 0 : g_motion.auto_center.hold_mdeg) : c->target_steering_mdeg;
        if (PATROL_CHASSIS_ACTUATION_ENABLED && PATROL_STEERING_EXECUTOR_ENABLED &&
            requested && safe && (!local_requested || local_moving) && g_motion.bench.phase == BENCH_IDLE) {
            if (center_requested)
                BenchSteering_StartCommissioning(&g_motion.bench, g_motion.diagnostics.steering_position_count,
                    g_motion.center_cal.start_driver_count, VEHICLE_CENTER_COMMISSIONING_LIMIT_MDEG, now);
            else
                BenchSteering_Start(&g_motion.bench, g_motion.diagnostics.steering_position_count,
                    g_motion.coordinate.runtime_center, cfg->steering_center_count,
                    CHASSIS_MAX_STEERING_MDEG, now);
        }
        safe = safe && !c->fault_latched;
        uint8_t drive_requested = executor_requested && remote_requested && !c->drive_brake;
        if (PATROL_CHASSIS_ACTUATION_ENABLED && PATROL_DRIVE_EXECUTOR_ENABLED && drive_requested && executor_safe &&
            g_motion.bench.phase == BENCH_ACTIVE && g_motion.drive.phase == BENCH_IDLE)
            BenchDrive_Start(&g_motion.drive, now);
        uint8_t drive_was_verifying = g_motion.drive.verify;
        uint8_t steer_was_verifying = g_motion.bench.verify;
        BenchDrive_Tick(&g_motion.drive, now, drive_requested, executor_safe,
            c->target_rpm, g_motion.diagnostics.left_actual_rpm, g_motion.diagnostics.right_actual_rpm,
            g_motion.diagnostics.left_rpm_ms, g_motion.diagnostics.right_rpm_ms);
        BenchSteering_Tick(&g_motion.bench, now,
            executor_requested && g_motion.drive.phase != BENCH_FAULT,
            center_executor_active && g_motion.center_cal.phase == CENTER_CAL_STOPPING ?
                stop_transport_safe : executor_safe,
            requested_angle, c->feedback.steering_actual_mdeg);
        if (!drive_was_verifying && g_motion.drive.verify == 1U)
            APP_LOGW("ACKVERIFY", "drive step=%u: lost write ACK; uploads only, limit=%u ms",
                (unsigned)g_motion.drive.step, (unsigned)BENCH_WRITE_VERIFY_MS);
        if (!steer_was_verifying && g_motion.bench.verify == 1U)
            APP_LOGW("ACKVERIFY", "steer step=%u: lost write ACK; uploads only, limit=%u ms",
                (unsigned)g_motion.bench.step, (unsigned)BENCH_WRITE_VERIFY_MS);
        g_motion.diagnostics.motion_priority = CenterCalibration_IsActive() ||
            (g_motion.bench.phase != BENCH_IDLE && g_motion.bench.phase != BENCH_FAULT) ||
            (g_motion.drive.phase != BENCH_IDLE && g_motion.drive.phase != BENCH_FAULT);
        if (g_motion.bench.phase == BENCH_STOPPING || g_motion.bench.phase == BENCH_FAULT) {
            if (!CenterCalibration_IsActive() &&
                (!remote_requested || !safe || g_motion.bench.phase == BENCH_FAULT)) RemoteService_Revoke();
            if (!g_motion.bench_transport_stopped) {
                g_motion.diagnostics.pending_valid = 0;
                PatrolRuntimePort_AbortCanTx();
                g_motion.bench_transport_stopped = 1;
                /* Send shutdown first, then alternate confirmation and safety reads. */
                g_motion.bench_turn = 1;
            }
        } else g_motion.bench_transport_stopped = 0;
    }
    PatrolRuntimePort_Exit();
    now = PatrolRuntime_ApplyCut(now);
    if (service_fault_stop()) return;
    /* Never write flash while powered or inside the safety critical section. */
    if (fault_dirty && !VehiclePowerOutput_IsOn() && !PatrolRuntimePort_OtaBusy()) {
        fault_storage=OTA_Info_SaveMotionFault(&last_fault) ? 1U : 2U;
        fault_dirty=0;
    }
    /* >>>>> DRIVE-RECOVERY: START >>>>> */
    if (VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
        PatrolRuntimePort_CanTxReady())
    {
        PatrolRuntimePort_Enter();
        if (recovery_wanted(now)) {
            PatrolCanFrame rec;
            uint8_t ok1, ok2;
            /* The drives ignore NMT pre-operational while in Operation Enabled;
             * the CiA402 shutdown controlword is what actually releases them. */
            rec = PatrolCanopen_SdoWrite16(PATROL_CANOPEN_STEERING_NODE, 0x6040U, 0U, 6U);
            ok1 = PatrolRuntimePort_SendCan(&rec);
            rec = PatrolCanopen_SdoWrite16(PATROL_CANOPEN_DRIVE_NODE, 0x6040U, 0U, 6U);
            ok2 = PatrolRuntimePort_SendCan(&rec);
            APP_LOGI("RECOVER", "shutdown steer=%u drive=%u", (unsigned)ok1, (unsigned)ok2);
        }
        PatrolRuntimePort_Exit();
    }
    /* <<<<< DRIVE-RECOVERY: END <<<<< */
    if (VehiclePowerOutput_IsOn() && !PatrolRuntimePort_CanBusOffActive() &&
        PatrolRuntimePort_CanTxReady())
    {
        PatrolRuntimePort_Enter();
        have_request = PatrolRuntime_Take(now, &request, &bench_frame);
        PatrolRuntimePort_Exit();
    }
    if (have_request)
    {
        PatrolRuntimePort_Enter();
        /* 高优先级安全任务可能在普通帧选出后启动故障停机；任何旧 owner 的帧都必须丢弃。 */
        if (fault_stop.active || RemoteService_State()->fault_pending) have_request = 0U;
        /* A traction inhibit can arrive after frame selection. Discard an old drive
         * command and start its bounded zero/disable sequence, without touching
         * the steering executor or the shared session. */
        if (bench_frame == 2 && RemoteService_State()->drive_brake &&
            g_motion.drive.phase != BENCH_STOPPING && g_motion.drive.phase != BENCH_IDLE) {
            BenchDrive_Stop(&g_motion.drive, PatrolRuntimePort_Now(), 0U);
            have_request = 0U;
        }
        /* A higher priority safety tick may have revoked the selected command. */
        if (have_request && bench_frame && g_motion.center_cal.phase != CENTER_CAL_JOGGING &&
            g_motion.center_cal.phase != CENTER_CAL_STOPPING && (bench_frame == 1 ?
            (g_motion.bench.phase != BENCH_STOPPING && g_motion.bench.phase != BENCH_IDLE) :
            (g_motion.drive.phase != BENCH_STOPPING && g_motion.drive.phase != BENCH_IDLE)) &&
             (!RemoteService_State()->session_active || !RemoteService_State()->enable_request ||
              (int32_t)(PatrolRuntimePort_Now() - RemoteService_State()->lease_deadline_ms) >= 0) &&
             !(bench_frame == 1 && g_motion.auto_center.requested &&
               auto_center_safe(RemoteService_State(), PatrolRuntimePort_Now()))) {
            BenchSteering_Stop(&g_motion.bench, PatrolRuntimePort_Now(), 0);
            BenchDrive_Stop(&g_motion.drive, PatrolRuntimePort_Now(), 0);
            have_request = 0;
        }
        /* Each compiled executor is restricted to its own node. */
        if (have_request && ((bench_frame == 1 && PATROL_STEERING_EXECUTOR_ENABLED &&
                (request.id == 0x601U || (request.id == 0 && request.data[1] == 1))) ||
              (bench_frame == 2 && PATROL_DRIVE_EXECUTOR_ENABLED &&
                (request.id == 0x602U || (request.id == 0 && request.data[1] == 2))) ||
             (!bench_frame && (request.id == 0x601U || request.id == 0x602U) && request.data[0] == 0x40U)) &&
            !PatrolRuntimePort_SendCan(&request))
        {
            PatrolRuntimePort_Enter();
            if (bench_frame == 2) BenchDrive_TxFailed(&g_motion.drive);
            else if (bench_frame == 1) BenchSteering_TxFailed(&g_motion.bench);
            else PatrolDiagnostics_OnSdoTxError(&g_motion.diagnostics, now);
            PatrolRuntimePort_Exit();
        }
        PatrolRuntimePort_Exit();
    }
    if ((int32_t)(now - feedback_due) >= 0) {
        PatrolRuntime_UpdateFeedback(now);
    }
}

// 为编码器任务取得快照并领取一次配置写入请求。领取动作在临界区内将请求改为进行中，避免重复写入；返回非零表示本周期应执行编码器写事务。
uint8_t PatrolRuntime_BeginEncoderCycle(PatrolDiagnosticsSnapshot* snapshot)
{
    uint8_t write_center = 0;
    PatrolRuntimePort_Enter();
    PatrolDiagnostics_Snapshot(&g_motion.diagnostics, PatrolRuntimePort_Now(), snapshot);
    if (g_motion.center_cal.encoder_write_state == 1U) {
        g_motion.center_cal.encoder_write_state = 4U;
        write_center = 1U;
    }
    PatrolRuntimePort_Exit();
    return write_center;
}

// 发布编码器配置写入或验证的结果状态。在临界区更新标定写状态，后续标定流程据此决定提交或故障，不在此写 Flash。
void PatrolRuntime_EncoderWriteCompleted(uint8_t state)
{
    PatrolRuntimePort_Enter();
    g_motion.center_cal.encoder_write_state = state;
    PatrolRuntimePort_Exit();
}

// 接收编码器寄存器配置并更新诊断。调用方须保护诊断共享上下文；仅登记本次读到的参数，不自行改变规格或标定。
void PatrolRuntime_OnEncoderConfig(const uint16_t* registers, uint32_t now)
{
    PatrolDiagnostics_OnEncoderConfig(&g_motion.diagnostics, registers, now);
}

// 接收绝对编码器计数及其采样时间。调用方持有运行层所需保护，计数随后参与角度、量程和新鲜度判断。
void PatrolRuntime_OnEncoderCount(uint32_t count, uint32_t now)
{
    PatrolDiagnostics_OnEncoderCount(&g_motion.diagnostics, count, now);
}

// 登记编码器通信或格式错误。错误分类交由诊断层累计和暴露，不用无效数据刷新成功采样时间。
void PatrolRuntime_OnEncoderError(PatrolEncoderError error)
{
    PatrolDiagnostics_OnEncoderError(&g_motion.diagnostics, error);
}
