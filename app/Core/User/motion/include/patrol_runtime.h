#ifndef PATROL_RUNTIME_H
#define PATROL_RUNTIME_H
#include <stdint.h>
#include "motion_fault.h"
// 在安全前提满足时申请清除 MCU 运动故障。复位路径检查输出、OTA、标定与执行器上下文；这是软件故障复位入口，不保证外部驱动器故障已消失。
uint8_t PatrolRuntime_ResetFault(void);
// 请求独立故障停机并优先发送两节点撤使能；非零表示异步确认仍在进行，期间禁止普通运动发送。
uint8_t PatrolRuntime_RequestFaultStop(void);
// 开启一个新的故障记录周期。仅重置本轮首故障捕获标志，后续首因由 CaptureFault 保存，不等同于清除驱动器故障。
void PatrolRuntime_BeginFaultEpisode(void);
// 在本轮首次故障时冻结运行上下文。记录来源、原因、门禁、双轮反馈和执行器状态并标记待存储；Flash 写入延后到断开输出且 OTA 不忙时。
void PatrolRuntime_CaptureFault(uint32_t source, uint32_t reason, uint32_t gates);
// 复制最近一次运动故障记录。在临界区读取固定快照，记录是否存在由返回值表示；故障记录与实时反馈是不同证据。
uint8_t PatrolRuntime_GetFault(MotionFaultRecord* record);
// 读取运动故障记录的持久化状态码。返回当前存储标记，区分待处理、写入成功与写入失败，不能据此认定车辆已停车。
uint8_t PatrolRuntime_FaultStorage(void);
#include "patrol_diagnostics.h"

// 取得带当前时间判断的运动诊断快照。在运行端口临界区内统一取样，供业务查询使用；调用者不应把旧快照当作当前运动许可。
void PatrolRuntime_GetDiagnostics(PatrolDiagnosticsSnapshot* snapshot);
// 初始化运动诊断、执行器与标定运行上下文。载入既有故障证据并建立安全默认状态，不自动恢复遥控会话或发出运动目标。
void PatrolRuntime_Init(void);
// 查询本地自动回中是否持有转向请求。供业务状态和任务协调使用；不把请求存在等同于回中已经完成。
uint8_t PatrolRuntime_AutoCenterActive(void);
// 执行一次运动运行层的非阻塞调度。仲裁遥控、标定和回中，推进双执行器并处理切电、故障存储与 CAN 请求；发送前再次检查会话和租约，防止选帧后授权被撤销。
void PatrolRuntime_Tick(void);
// 将驱动器 CAN 反馈分发给诊断与执行器。同一帧可用于状态维护和在途响应匹配；调用方须按运行层并发约定保护共享状态。
void PatrolRuntime_OnCanFrame(const PatrolCanFrame* frame, uint32_t now);
// 为编码器任务取得快照并领取一次配置写入请求。领取动作在临界区内将请求改为进行中，避免重复写入；返回非零表示本周期应执行编码器写事务。
uint8_t PatrolRuntime_BeginEncoderCycle(PatrolDiagnosticsSnapshot* snapshot);
// 发布编码器配置写入或验证的结果状态。在临界区更新标定写状态，后续标定流程据此决定提交或故障，不在此写 Flash。
void PatrolRuntime_EncoderWriteCompleted(uint8_t state);
/* Encoder callbacks are invoked under the encoder task's critical section. */
// 接收编码器寄存器配置并更新诊断。调用方须保护诊断共享上下文；仅登记本次读到的参数，不自行改变规格或标定。
void PatrolRuntime_OnEncoderConfig(const uint16_t* registers, uint32_t now);
// 接收绝对编码器计数及其采样时间。调用方持有运行层所需保护，计数随后参与角度、量程和新鲜度判断。
void PatrolRuntime_OnEncoderCount(uint32_t count, uint32_t now);
// 登记编码器通信或格式错误。错误分类交由诊断层累计和暴露，不用无效数据刷新成功采样时间。
void PatrolRuntime_OnEncoderError(PatrolEncoderError error);
#endif
