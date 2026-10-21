#include "ota_service.h"

#include <stdio.h>
#include <string.h>

#include "abox_boot_v2.h"
#include "abox_boot_v2_app.h"
#include "abox_boot_v2_services.h"
#include "abox_boot_v2_ca_pem.h"
#include "app_log.h"
#include "app_version.h"
#include "boot_cfg.h"
#include "mqtt_core.h"
#include "mqtt_runtime.h"
#include "main.h"
#include "patrol_config.h"

static ABoxBootV2Ec800Adapter g_adapter;

// 为公共 OTA 适配器提供活动 MQTT client 索引。直接读取运行时的实际索引，避免假设模块永远使用固定 client。
static uint8_t adapter_mqtt_client_index(void *context)
{
    (void)context;
    return MqttRuntime_ActiveClient();
}

// 把公共 OTA 日志等级映射为产品日志。保留消息文本并按错误、警告或信息输出，不在此记录凭据或执行升级动作。
static void adapter_log(void *context, uint8_t level, const char *message)
{
    (void)context;
    if (level >= 3U) APP_LOGE("OTA", "%s", message);
    else if (level == 2U) APP_LOGW("OTA", "%s", message);
    else APP_LOGI("OTA", "%s", message);
}

// 将产品布局、版本、CA 和共享 EC800 服务绑定到公共 OTA。先使旧初始化就绪状态失效，再绑定冻结分区和当前镜像尺寸；绑定失败时保持 OTA 不可用。
void OtaService_Init(Ec800AtCore_t *at)
{
    static ABoxBootV2AppPort port;
    static ABoxBootV2AppConfig config;
    static const ABoxBootV2Layout layout = {
        BOOT_STATE_A_ADDR, BOOT_STATE_B_ADDR, BOOT_STATE_PAGE_SIZE,
        APP_START_ADDR, APP_END_ADDR, 0x20000000U, 0x20010000U
    };
    const PatrolRemoteConfigV5_t *runtime = PatrolConfig_Get();
    extern uint8_t __app_image_end;

    /* Invalidate startup readiness even if binding fails before App Init. */
    (void)ABoxBootV2App_Init(NULL, NULL);
    memset(&port, 0, sizeof(port));
    if (!ABoxBootV2Ec800Adapter_BindServices(&g_adapter, at, MQTT_Core_Bridge(), &port)) {
        APP_LOGE("OTA", "MQTT bridge not ready for OTA binding");
        return;
    }
    port.mqtt_client_index = adapter_mqtt_client_index;
    port.log = adapter_log;

    memset(&config, 0, sizeof(config));
    config.ota_origin = runtime->ota_origin;
    config.revision_product = "patrol_vehicle_chassis";
    config.artifact_name = "patrol_vehicle_chassis_app.bin";
    config.version_prefix = "patrol-chassis-";
    config.running_version = Patrol_AppFwVersion();
    config.ca_pem = (const uint8_t *)g_ota_ca_pem;
    config.ca_pem_length = (uint32_t)(sizeof(g_ota_ca_pem) - 1U);
    config.app_start_addr = APP_START_ADDR;
    config.app_size = (uint32_t)(&__app_image_end - (uint8_t *)APP_START_ADDR);
    config.app_max_size = APP_MAX_SIZE;
    if (!ABoxBootV2_StateBindLayout(&layout) ||
        !ABoxBootV2App_Init(&port, &config)) {
        APP_LOGE("OTA", "OTA service initialization failed; OTA disabled");
    }
}

// 查询公共 OTA App 服务是否完成初始化。仅说明服务已绑定，下载条件、预置和忙状态仍需分别检查。
uint8_t OtaService_IsInitialized(void) { return (uint8_t)ABoxBootV2App_IsInitialized(); }
// 请求开始 OTA/TLS 所需模块文件预置。异步请求由公共服务推进，返回接受结果不代表文件已写好或校验完成。
uint8_t OtaService_BeginProvisioning(void) { return (uint8_t)ABoxBootV2App_BeginProvisioning(); }
// 推进公共 OTA App 服务一次。在统一云任务与 AT 资源仲裁下调用，产品不复制公共下载或安装状态机。
void OtaService_Task(void) { ABoxBootV2App_Task(); }
// 查询 OTA 所需模块文件是否已经预置完成。供 TLS 和注册前置门禁使用，不表示存在可安装固件。
uint8_t OtaService_IsProvisioned(void) { return (uint8_t)ABoxBootV2App_IsProvisioned(); }
// 查询 OTA 服务是否具备接收新请求的就绪条件。不能仅凭初始化成功或网络连接成功替代此判断。
uint8_t OtaService_IsReady(void) { return (uint8_t)ABoxBootV2App_IsReady(); }
// 查询 OTA 是否占用升级或预置流程。该结果参与运动禁止与共享 AT/Flash 互斥，忙时不能绕过执行运动。
uint8_t OtaService_IsBusy(void) { return (uint8_t)ABoxBootV2App_IsBusy(); }
// 取得公共 OTA 当前阶段名称。返回服务持有的只读文本，用于内部诊断，不作为现场升级验收结论。
const char *OtaService_Phase(void) { return ABoxBootV2App_Phase(); }
// 读取公共 OTA 保存的最近错误码。零值或其他编号含义遵循公共 OTA 服务定义，不自行清除原始错误。
uint32_t OtaService_LastError(void) { return ABoxBootV2App_LastError(); }

// 把已解析的产品 OTA 请求转为公共升级请求。核对初始化、指针及 URL/版本容量，再传递大小与 CRC；接受请求不代表下载、烧写或启动成功。
uint8_t OtaService_Start(const CloudProtocolRequest_t *request)
{
    ABoxBootV2AppRequest common;
    size_t url_length;
    size_t version_length;
    if (!OtaService_IsInitialized() || !request) return 0U;
    url_length = strlen(request->ota_url);
    version_length = strlen(request->ota_version);
    if (url_length >= sizeof(common.url) || version_length >= sizeof(common.version)) return 0U;
    memset(&common, 0, sizeof(common));
    memcpy(common.url, request->ota_url, url_length + 1U);
    memcpy(common.version, request->ota_version, version_length + 1U);
    common.size = request->ota_size;
    common.crc32 = request->ota_crc32;
    return (uint8_t)ABoxBootV2App_Start(&common);
}

// 消费一次安装准备完成事件。非零表示该事件可由业务层安排 INSTALL_PENDING 响应和后续重启，不能重复消费为多次成功。
uint8_t OtaService_TakeInstallReady(void) { return (uint8_t)ABoxBootV2App_TakeInstallReady(); }
// 消费一次 OTA 失败事件并取得错误码。仅在存在新失败时写出 error 并返回非零，供响应调度使用。
uint8_t OtaService_TakeFailure(uint32_t *error) { return (uint8_t)ABoxBootV2App_TakeFailure(error); }
// 请求公共 Boot State 确认回滚报告已处理。修改状态仍受公共存储校验约束；本接口不把回滚后的固件重新标记为新候选已验收。
void OtaService_AcknowledgeRollback(void) { (void)ABoxBootV2_AcknowledgeRollback(); }
// 返回当前 OTA 目标版本文本。指针由公共服务持有，跨任务或后续请求需复制，不得修改或释放。
const char *OtaService_TargetVersion(void) { return ABoxBootV2App_TargetVersion(); }
