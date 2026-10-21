#ifndef PATROL_MQTT_RUNTIME_H
#define PATROL_MQTT_RUNTIME_H

#include <stdint.h>
#include "abox_mqtt_runtime.h"
// 查询 TLS/MQTT 运行时是否要求模块恢复。仅提供恢复意图，实际电源与重启时机由 App 服务统一仲裁。
uint8_t MqttRuntime_NeedsModemRecovery(void);

#define PATROL_MQTT_TLS_CONTEXT 2U
#define PATROL_MQTT_TLS_PROFILE_DEFAULT 0U

typedef ABoxMqttRuntimeState MqttRuntimeState;
#define MQTT_RUNTIME_IDLE ABOX_MQTT_RUNTIME_IDLE
#define MQTT_RUNTIME_DISCONNECT ABOX_MQTT_RUNTIME_DISCONNECT
#define MQTT_RUNTIME_UNBIND ABOX_MQTT_RUNTIME_UNBIND
#define MQTT_RUNTIME_WAIT_CA ABOX_MQTT_RUNTIME_WAIT_CA
#define MQTT_RUNTIME_CLOCK ABOX_MQTT_RUNTIME_CLOCK
#define MQTT_RUNTIME_PREPARE ABOX_MQTT_RUNTIME_PREPARE
#define MQTT_RUNTIME_BIND ABOX_MQTT_RUNTIME_BIND
#define MQTT_RUNTIME_PREPARED ABOX_MQTT_RUNTIME_PREPARED
#define MQTT_RUNTIME_CONNECT ABOX_MQTT_RUNTIME_CONNECT
#define MQTT_RUNTIME_CONNECTED ABOX_MQTT_RUNTIME_CONNECTED
#define MQTT_RUNTIME_READY ABOX_MQTT_RUNTIME_READY
#define MQTT_RUNTIME_FAILED ABOX_MQTT_RUNTIME_FAILED

// 绑定公共运行时、MQTT 核心端口与安全检查回调。加载标准 EC800 选项并启用模块时钟模式查询，产品仅提供配置与状态适配。
int MqttRuntime_Init(ABoxEc800At *at, const ABoxMqttConfig *config, uint32_t now);
// 按当前单调时间轮询 TLS/MQTT 运行状态机。同时更新供回调使用的时间基准，由云任务串行调度。
void MqttRuntime_Task(uint32_t now);
/* RAM-only 7.1 entry. Caller credentials are copied; no Flash API is called. */
// 提交并开始应用 RAM 候选运行配置。复用公共运行时，不在此保存为正式 Config V5。
int MqttRuntime_ApplyRamCandidate(const ABoxMqttConfig *config, uint32_t now);
// 请求恢复指定的稳定 RAM 运行配置。恢复也是异步连接过程，最终证明交给试连管理器，失败不能默认为已恢复。
int MqttRuntime_RestoreRamActive(const ABoxMqttConfig *config, uint32_t now);
// 在配置恢复失败时锁定运行时。阻止自动连接继续掩盖不一致配置，等待明确的维护恢复路径。
void MqttRuntime_BlockForRestoreFailure(void);
// 校验并暂存一份仅 RAM 的候选运行配置。此时尚未激活连接，也不写 Flash，便于先回复旧连接上的维护请求。
int MqttRuntime_StageRamCandidate(const ABoxMqttConfig *config);
// 在适当响应时序后激活已暂存候选。更新运行时间基准并启动公共切换流程，结果仍需连接、订阅与发布证明。
int MqttRuntime_ActivateStagedCandidate(uint32_t now);
// 查询公共运行时是否已经进入 READY。表示连接流程达到定义的就绪阶段，不代表候选已持久化提交。
int MqttRuntime_IsReady(void);
// 停止首次注册试连建立的运行连接。返回公共停止接口的进度值，调用方在异步恢复阶段继续轮询，不能当作普通布尔完成标志。
int MqttRuntime_StopFirst(void);
// 取得 TLS/MQTT 运行时阶段枚举。用于调度和诊断，状态只能由公共运行时操作推进。
MqttRuntimeState MqttRuntime_State(void);
// 读取当前使用的 EC800 MQTT client 索引。索引用于 OTA 等服务与当前连接共享资源，不是布尔结果。
uint8_t MqttRuntime_ActiveClient(void);
// 查询当前活动连接是否采用 TLS。反映实际运行时选择，不等同于配置中期望开启 TLS 的字段。
uint8_t MqttRuntime_ActiveTls(void);
// 读取当前活动 PDP/TLS 上下文索引。供共享模块服务正确选择资源，返回的是索引编号而非是否成功。
uint8_t MqttRuntime_ActiveContext(void);
// 返回当前活动运行配置的只读视图。内含运行时持有的字符串，调用者若需跨切换保留必须复制，不能修改或释放内部存储。
const ABoxMqttConfig *MqttRuntime_ActiveConfig(void);
// 返回已暂存但尚未激活配置的只读视图。仅用于试连管理器复制与核验，指针不代表该配置已经成功连接。
const ABoxMqttConfig *MqttRuntime_StagedConfig(void);
// 通知运行时底层模块已经复位。清理旧模块连接假设，后续按统一状态机恢复，不能保留原连接就绪证明。
void MqttRuntime_OnModemReset(void);

#endif
