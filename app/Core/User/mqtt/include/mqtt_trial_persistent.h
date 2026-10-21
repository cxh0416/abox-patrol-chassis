#ifndef PATROL_MQTT_TRIAL_PERSISTENT_H
#define PATROL_MQTT_TRIAL_PERSISTENT_H
#include <stdint.h>
#include "patrol_config.h"
// 建立同时验证 Config V5 和 TLS 凭据的持久候选。先校验身份、云配置和凭据，再暂存运行配置；只有后续证明与提交阶段完成才写入稳定配置。
int MqttTrialManager_StagePersistentPsk(const PatrolRemoteConfigV5_t*, const ABoxTlsCredentials*, uint64_t, uint32_t);
// 使用当前 TLS 凭据建立持久 MQTT 配置候选。复用带凭据的试连入口，避免绕过统一候选与恢复校验。
int MqttTrialManager_StagePersistent(const PatrolRemoteConfigV5_t *candidate, uint64_t proof_id, uint32_t now);
#endif
