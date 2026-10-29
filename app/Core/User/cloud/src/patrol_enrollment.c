#include "patrol_enrollment.h"

#include <stdio.h>
#include <string.h>

#include "abox_enrollment.h"
#include "abox_enrollment_service.h"
#include "abox_stm32f105_ec800_identity.h"
#include "app_version.h"
#include "cloud_internal.h"
#include "cloud_service.h"
#include "ec_power.h"
#include "main.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "mqtt_trial_manager.h"
#include "ota_service.h"
#include "patrol_config.h"

#define ENROLL_OWNER (ABOX_EC800_OWNER_PRODUCT_BASE + 2U)

static ABoxEnrollmentService g_service;
static ABoxEnrollmentIdentityStorage g_identity_storage;
static char g_request_id[48];
static char g_poll_token[64];

// 提供身份注册服务的单调毫秒时钟。user 为平台回调参数，本产品适配器不使用；返回 HAL tick。
static uint32_t now_ms(void *user)
{ (void)user; return HAL_GetTick(); }
// 检查身份注册所需模块、TLS 文件和 OTA 空闲条件。模块上电完成且预置可用后才允许注册网络请求，避免占用升级事务。
static int network_ready(void *user)
{
    (void)user;
    return EC_Power_IsOnDone() && OtaService_IsProvisioned() && !OtaService_IsBusy();
}
// 向公共注册服务报告产品是否需要注册。受控 PSK 工位模式禁止 HTTP 注册，其他构建使用产品配置判断。
static int needs_enrollment(void *user)
{
    (void)user;
#ifdef ABOX_PSK_STATION
    /* Controlled first injection owns enrollment scratch; no HTTP enrollment. */
    return 0;
#else
    return CloudService_NeedsEnrollment();
#endif
}
// 按注册阶段暂停或恢复 MQTT 核心。与注册共享 AT 通道的仲裁入口，不改变底盘运动目标。
static void mqtt_pause(void *user, uint8_t pause)
{ (void)user; MQTT_Core_SetPaused(pause); }

// 构造首次注册使用的产品硬件与固件身份。由平台身份构造器填写 UID 和产品信息，VID 是否可用由当前配置及已注入标记决定。
static int identity(void *user, ABoxEnrollmentIdentity *out)
{
    const PatrolRemoteConfigV5_t *cfg = PatrolConfig_Get();
    (void)user;
    if (!cfg || !out) return 0;
    return ABoxStm32F105Ec800Identity_Build(out, &g_identity_storage,
        "", cfg->vid,
        (uint8_t)(strcmp(cfg->vid, PATROL_CONFIG_DEFAULT_VID) != 0 ||
                  cfg->reserved0 == 1U), "PATROL-",
        "patrol_vehicle_chassis_stm32f105_ec800_v1",
        Patrol_AppFwVersion());
}

// 把注册返回的凭据转换为受监督的 MQTT 试连。先构造运行配置，试连启动成功才标记配置可用，不能在接收凭据时直接宣称持久化成功。
static int start_trial(void *user, const ABoxEnrollmentCredential *credential)
{
    ABoxMqttConfig runtime;
    (void)user;
    if (!credential) return 0;
    runtime = (ABoxMqttConfig){credential->host, credential->port,
                              credential->username, credential->password,
                              credential->tls_enabled, credential->tls_profile_id};
    runtime.tls_credentials = credential->tls_credentials;
    if (!MqttTrialManager_StartEnrollment(&runtime, credential->vid, HAL_GetTick())) return 0;
    g_cloud.config_ready = 1U;
    return 1;
}

// 取消身份注册发起的 MQTT 候选试连。交给试连管理器按现有回退流程恢复，不直接改写稳定配置。
static void cancel_trial(void *user)
{ (void)user; MqttTrialManager_Cancel(HAL_GetTick()); }
// 报告身份注册所依赖的试连最终结果。提交成功返回 1，失败或恢复失败返回 0，尚在进行返回 -1；失败同时撤销 config_ready。
static int trial_result(void *user)
{
    ABoxMqttTrialState state;
    (void)user;
    state = MqttTrialManager_State();
    if (state == ABOX_MQTT_TRIAL_COMMITTED) return 1;
    if (state == ABOX_MQTT_TRIAL_FAILED || state == ABOX_MQTT_TRIAL_RESTORE_FAILED) {
        g_cloud.config_ready = 0U;
        return 0;
    }
    return -1;
}

// 绑定公共注册服务与产品回调及固定缓冲区。注册 URL、响应和请求 ID 使用明确容量的共享存储，复用平台服务而不复制注册状态机。
uint8_t PatrolEnrollment_Init(ABoxEc800At *at)
{
    ABoxEnrollmentServicePort port = {0, now_ms, network_ready,
        needs_enrollment, identity, mqtt_pause, start_trial, trial_result,
        cancel_trial};
    ABoxEnrollmentBuffers buffers = {0};
    buffers.url = g_cloud.last_response_payload; buffers.url_size = 192U;
    buffers.body = g_cloud.response_payload; buffers.body_size = 512U;
    buffers.response = g_cloud.tx_payload; buffers.response_size = 769U;
    buffers.request_id = g_request_id; buffers.request_id_size = sizeof(g_request_id);
    buffers.poll_token = g_poll_token; buffers.poll_token_size = sizeof(g_poll_token);
    return (uint8_t)ABoxEnrollmentService_Init(&g_service, at, ENROLL_OWNER,
        "UFS:ota_ca.pem", PatrolConfig_Get()->ota_origin, &buffers,
        PATROL_MQTT_TLS_PROFILE_DEFAULT, &port);
}

// 轮询当前身份注册状态机。由云任务统一调用，避免与其他 AT 使用者并行推进同一服务。
void PatrolEnrollment_Task(void)
{
    ABoxEnrollmentService_Poll(&g_service);
}

// 通知注册服务模块已复位。使旧模块会话和在途假设失效，后续由公共服务重建所需状态。
void PatrolEnrollment_AfterModemReset(void)
{
    ABoxEnrollmentService_AfterModemReset(&g_service);
}
