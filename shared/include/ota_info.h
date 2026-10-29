#ifndef PATROL_CHASSIS_OTA_INFO_H
#define PATROL_CHASSIS_OTA_INFO_H

#include <stdint.h>
#include "abox_tls_secret_record.h"

#include "boot_cfg.h"

#define PATROL_CONFIG_MAGIC 0x56435543U /* On-flash value is frozen. */
#define PATROL_CONFIG_VERSION 5U
#define PATROL_CONFIG_TLS_PROFILE_DEFAULT 0U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t checksum;
    uint8_t reserved_remote; /* Former remote_enabled byte: preserve Config V5 layout. */
    uint8_t diag_log_enabled;
    uint8_t diag_log_level;
    uint8_t reserved0;
    char vid[32];
    char mqtt_host[64];
    uint16_t mqtt_port;
    uint16_t diag_log_port;
    char mqtt_username[32];
    char mqtt_password[64];
    char diag_log_host[64];
    char ota_origin[96];
    int32_t steering_center_count;
    uint32_t absolute_center_count;
    uint32_t absolute_counts_per_degree_milli;
    uint16_t steering_tolerance_mdeg;
    int8_t steering_direction;
    int8_t absolute_direction;
    uint8_t calibrated;
    uint8_t reserved1;
    uint32_t steering_counts_per_degree_milli;
    uint8_t mqtt_tls_enabled;
    uint8_t mqtt_tls_profile_id;
    uint8_t reserved2[2];
} PatrolRemoteConfigV5_t;

typedef struct
{
    uint32_t ota_req_magic;
    uint32_t app_valid_magic;
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint32_t req_fail_count;
    char url[128];
    PatrolRemoteConfigV5_t remote_config;
    uint32_t reserved[8];
    ABoxTlsSecretRecord mqtt_tls_secret; /* Optional extension; frozen config ABI unchanged. */
} OTA_Info_t;

// 从配置页解码 TLS 凭据记录。复用平台密钥记录格式校验，调用者不得将凭据输出到普通日志或公共证据文件。
uint8_t OTA_Info_LoadMqttCredentials(ABoxTlsCredentials* credentials);
// 将 Config V5 与 TLS 凭据作为一个快照保存。先校验并编码凭据，再执行受恢复日志保护的页更新，最后同时复读比较配置与密钥记录。
uint8_t OTA_Info_SaveMqttSnapshot(const PatrolRemoteConfigV5_t* cfg, const ABoxTlsCredentials* credentials);
// 读取 OTA_Info_t 原始存储结构。必要时先尝试恢复日志；本函数不验证整个结构或各业务字段，调用者须使用相应有效性检查。
void OTA_Info_Load(OTA_Info_t* info);
// 将 OTA 信息结构保存到共享配置页。复用受保护的页更新流程，不可在不满足存储安全前提的上下文调用。
uint8_t OTA_Info_Save(const OTA_Info_t* info);
// 将 OTA_Info_t 全部置为擦除值并写回配置页。会同时清除该结构内的 Config V5、TLS 凭据和 OTA 字段，不能当作仅清 OTA 请求；不擦 Boot State 或整片 Flash。
uint8_t OTA_Info_Clear(void);

// 从配置页载入校验通过的 Config V5。仅在格式与校验均合法时复制到 cfg，失败由上层决定使用未标定默认值。
uint8_t OTA_Info_LoadRemoteConfig(PatrolRemoteConfigV5_t* cfg);
// 保存有效 Config V5 并复读核对内容。保留 OTA 信息其他字段，通过恢复日志保护更新，写后再次验证格式和逐字节一致性。
uint8_t OTA_Info_SaveRemoteConfig(const PatrolRemoteConfigV5_t* cfg);

// 检查兼容 OTA 请求标志及 URL 基本格式。仅说明存储请求结构可解释，不代表镜像尺寸、来源或 CRC 已通过安装校验。
uint8_t OTA_Info_IsRequestValid(const OTA_Info_t* info);
// 读取配置页并查询是否保留有效 OTA 请求。使用统一请求校验，不直接启动下载或复位。
uint8_t OTA_Info_HasRequest(void);
// 查询兼容 App 有效标志是否存在。该标志与 Boot State 的试运行确认和现场验收不是同一个概念。
uint8_t OTA_Info_AppMarkedValid(void);
// 设置兼容 App 有效标志并请求保存。复用配置页写入，本接口无返回值且忽略保存结果，调用方不能据调用完成宣称落盘成功。
void OTA_Info_MarkAppValid(void);
// 清除兼容 App 有效标志并请求保存。仅修改该兼容字段，底层写失败不会经本接口返回。
void OTA_Info_ClearAppValid(void);
// 登记兼容 OTA 镜像大小、CRC 与下载 URL。有界复制 URL、重置失败计数，无效请求清除标志；接口忽略底层保存结果，不能作为升级成功证明。
void OTA_Info_SetRequest(uint32_t fw_size, uint32_t fw_crc32, const char* url);
// 清除兼容 OTA 请求及失败计数。保留无关配置字段，页更新仍通过统一存储流程，接口不返回保存是否成功。
void OTA_Info_ClearRequest(void);
// 读取有效兼容 OTA 请求的失败计数。没有有效请求时返回零，计数不等同于 Boot State 的试运行失败次数。
uint32_t OTA_Info_GetReqFailCount(void);
// 将兼容 OTA 请求失败计数清零并请求保存。不清除其他升级状态，底层写入结果未通过本接口返回。
void OTA_Info_ClearReqFailCount(void);
// 对有效兼容 OTA 请求的失败计数作饱和递增。无有效请求时不写，达到最大值后不回绕，保存结果由当前兼容接口忽略。
void OTA_Info_IncReqFailCount(void);

#endif
