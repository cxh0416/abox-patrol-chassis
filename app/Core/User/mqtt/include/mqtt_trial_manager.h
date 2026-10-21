#ifndef PATROL_MQTT_TRIAL_MANAGER_H
#define PATROL_MQTT_TRIAL_MANAGER_H
#include <stdint.h>
#include "abox_mqtt_trial.h"
// 初始化产品试连上下文并绑定公共执行器。清空候选、稳定快照与恢复标志，公共执行器初始化成功后才接受新试连。
void MqttTrialManager_Init(void);
// 校验并建立一轮 RAM 候选试连。拒绝并行维护或恢复失败状态，保存稳定与候选快照并设置证明及超时窗口，尚不切换连接。
int MqttTrialManager_Stage(const ABoxMqttConfig *candidate, uint64_t proof_id, uint32_t now);
// 在接受响应发出后通知试连可以继续。向公共试连状态机提交与当前会话关联的 ACCEPTED 事件，而非直接标记已验证。
void MqttTrialManager_Accepted(uint32_t now);
// 推进试连执行、证明、持久化提交及恢复。一次轮询按公共维护服务推进，提交 Config V5 与凭据结果必须检查；失败恢复未成功时维持阻塞状态。
void MqttTrialManager_Poll(uint32_t now);
// 取消当前试连并进入既有恢复流程。清除延后取消标志并提交公共取消事件，保存状态日志供维护排查。
void MqttTrialManager_Cancel(uint32_t now);
// 登记等待响应完成后再取消试连的意图。保护旧连接上的取消响应时序，真正取消由后续完成回调触发。
void MqttTrialManager_DeferCancel(void);
// 将 TLS/MQTT 运行时变化转发给公共维护服务。READY 时重新记录发布证明起点，首次注册恢复阶段使用独立的停止完成路径。
void MqttTrialManager_OnRuntimeState(ABoxMqttRuntimeState state, uint32_t now);
// 用新的成功心跳发布证明候选或恢复连接。核对初始化、双层就绪、发布序号变化和取消意图，旧连接或旧序号不能完成本轮试连。
void MqttTrialManager_OnHeartbeatProof(uint32_t publish_sequence, uint32_t now);
// 返回当前 MQTT 试连阶段枚举。只读状态查询，用于响应和互斥门禁，不改变试连进度。
ABoxMqttTrialState MqttTrialManager_State(void);
// 判断是否允许自动运行时或模块恢复。Flash 恢复失败或试连处于 RESTORE_FAILED 时返回不允许，防止自动重连掩盖配置故障。
uint8_t MqttTrialManager_RuntimeRecoveryAllowed(void);
// 返回当前试连的 64 位会话标识。用于关联阶段事件与证明，不可将其用作底盘运动会话授权。
uint64_t MqttTrialManager_Session(void);
// 为首次注册建立专用候选试连。没有既有稳定连接可恢复时标记 first，失败通过停止首次连接退出，不能伪造已有稳定 Broker。
int MqttTrialManager_StartEnrollment(const ABoxMqttConfig *candidate, const char *vid, uint32_t now);
#endif
