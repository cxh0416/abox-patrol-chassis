#ifndef PATROL_CHASSIS_CONFIG_H
#define PATROL_CHASSIS_CONFIG_H

#include <stdint.h>

// 检查已存标定比例、方向和容差是否匹配当前固件规格。所有关键标定常量必须一致，防止换硬件或固件参数后继续使用旧比例。
uint8_t PatrolConfig_CalibrationCompatible(void);
// 检查单圈编码器标定及中位规格是否就绪。要求已标定、比例兼容且绝对中位符合本产品规格，供正常运动门禁使用。
uint8_t PatrolConfig_SingleTurnProfileReady(void);

#include "ota_info.h"

#define PATROL_CONFIG_DEFAULT_VID "PATROL-CHASSIS-000"
#define PATROL_CONFIG_DEFAULT_MQTT_HOST ""
#define PATROL_CONFIG_DEFAULT_MQTT_PORT 0U
#ifndef PATROL_CONFIG_DEFAULT_MQTT_USER
#define PATROL_CONFIG_DEFAULT_MQTT_USER ""
#endif
#ifndef PATROL_CONFIG_DEFAULT_MQTT_PASS
#define PATROL_CONFIG_DEFAULT_MQTT_PASS ""
#endif
#define PATROL_CONFIG_DEFAULT_DIAG_HOST "36.153.162.171"
#define PATROL_CONFIG_DEFAULT_DIAG_PORT 30080U
#define PATROL_CONFIG_DEFAULT_DIAG_LEVEL 2U
#define PATROL_CONFIG_DEFAULT_OTA_ORIGIN "https://ota.zxwl.ntiov.com:20443"

// 构造未标定的 Config V5 安全默认值。填入产品标识、默认通信及转向规格，并计算校验；不自行写 Flash 或启用运动。
void PatrolConfig_ResetDefault(PatrolRemoteConfigV5_t* cfg);
// 加载 Flash 配置与 TLS 凭据，失败时使用默认值。启用明确车号时检查已有标定归属，不匹配仅撤销 RAM 标定标志，保留 Flash 供后续维护核验。
void PatrolConfig_Load(void);
// 返回当前 RAM Config V5 的只读指针。存储由模块持有，保存配置后内容可能变化；跨阶段使用独立快照，不能修改或释放返回指针。
const PatrolRemoteConfigV5_t* PatrolConfig_Get(void);
// 验证 VID 是否满足产品身份字符与长度限制。复用 Topic 组件安全检查，避免非法身份参与 Client ID 或 Topic 构造。
uint8_t PatrolConfig_IsVidValid(const char* vid);
// 取得当前 TLS 凭据的只读视图。返回静态敏感存储，不得修改、释放或写入普通日志。
const ABoxTlsCredentials* PatrolConfig_MqttCredentials(void);
/* Write/readback without changing the immutable RAM stable snapshot. */
// 保存经过规范化的配置和 MQTT 凭据快照。作为公共配置存储适配端口调用统一原子快照路径，返回持久化结果而非连接验证结果。
uint8_t PatrolConfig_WriteMqttSnapshot(const PatrolRemoteConfigV5_t*, const ABoxTlsCredentials*);
// 校验并保存 Config V5，成功后更新 RAM 活动配置。仅在底层复读验证成功后切换 RAM，避免失败保存把运行状态伪装成已落盘。
uint8_t PatrolConfig_Save(const PatrolRemoteConfigV5_t* cfg);
// 基于当前配置修改指定 MQTT 身份和 Broker 字段。空字符串指针或零端口按当前兼容语义保留旧值，最终复用统一保存校验与存储门禁。
uint8_t PatrolConfig_SetMqtt(const char* vid, const char* host, uint16_t port, const char* username,
                          const char* password);
// 修改诊断日志开关、地址、端口和等级。以当前配置为基础构造副本，约束日志等级并通过统一保存入口提交。
uint8_t PatrolConfig_SetDiagLog(uint8_t enabled, const char* host, uint16_t port, uint8_t level);
// 判断配置中的云身份和 Broker 必填项是否完整。校验 VID、地址、用户名、密码和非零端口，只是配置可用性，不表示网络已连通。
uint8_t PatrolConfig_IsCloudReady(const PatrolRemoteConfigV5_t* cfg);
// 检查 OTA 来源配置是否为安全有效的 HTTPS 文本。要求有效配置、HTTPS 前缀和受限字符，不能据此替代下载镜像完整性检查。
uint8_t PatrolConfig_IsOtaOriginValid(const PatrolRemoteConfigV5_t* cfg);
// 校验并保存正式双传感器中位标定。比例、方向和容差必须与本产品规格完全一致，通过统一 Config V5 保存后才能视为已落盘。
uint8_t PatrolConfig_SetCalibration(int32_t steering_center_count, uint32_t absolute_center_count,
                                 uint32_t steering_counts_per_degree_milli,
                                 uint32_t absolute_counts_per_degree_milli,
                                 int8_t steering_direction, int8_t absolute_direction,
                                 uint16_t tolerance_mdeg);
// 清除标定有效标志并保存配置。保留其他配置字段，保存失败返回零，不可只在 RAM 中清标志便宣称持久化完成。
uint8_t PatrolConfig_ClearCalibration(void);

#endif
