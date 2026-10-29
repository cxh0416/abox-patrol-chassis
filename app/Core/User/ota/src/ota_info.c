#include "ota_info.h"
#include "motion_fault.h"

#include <stddef.h>
#include <string.h>

#include "main.h"

_Static_assert(sizeof(OTA_Info_t) <= OTA_INFO_PAGE_SIZE, "OTA info must fit in one flash page");
_Static_assert(sizeof(PatrolRemoteConfigV5_t) == 400U, "Patrol Config V5 layout changed");
_Static_assert(offsetof(PatrolRemoteConfigV5_t, mqtt_tls_enabled) == 396U,
               "Patrol Config V5 TLS enabled offset changed");
_Static_assert(offsetof(PatrolRemoteConfigV5_t, mqtt_tls_profile_id) == 397U,
               "Patrol Config V5 TLS profile offset changed");
_Static_assert(offsetof(PatrolRemoteConfigV5_t, reserved2) == 398U,
               "Patrol Config V5 reserved tail offset changed");
_Static_assert(sizeof(((PatrolRemoteConfigV5_t*)0)->reserved2) == 2U,
               "Patrol Config V5 reserved tail size changed");

// 发现有效恢复日志时还原配置与最近故障。先擦写配置页、复读验证，成功后再清恢复页；失败保留恢复证据供后续重试，绝不修改 Boot State 页。
static uint8_t recovery_restore_if_needed(void);
static uint8_t info_write_busy;

// 读取配置页原始内容，必要时先尝试断电恢复。写入正忙时不递归恢复；读取结果仍须由调用者检查对应记录的格式和校验。
static void ota_info_read_raw(OTA_Info_t* info)
{
    if (!info_write_busy) (void)recovery_restore_if_needed();
    if (info != 0) memcpy(info, (const void*)OTA_INFO_ADDR, sizeof(*info));
}

// 计算配置记录校验值并跳过校验字段自身。使用固定种子和逐字节移位异或，不是加密摘要，size 与 checksum_offset 必须对应实际结构。
static uint32_t config_checksum_bytes(const void* value, uint16_t size, uint16_t checksum_offset)
{
    const uint8_t* p = (const uint8_t*)value;
    uint32_t sum = 0x13572468U;
    uint16_t i;

    if (value == 0) return 0U;
    for (i = 0U; i < size; i++)
    {
        if ((i >= checksum_offset) && (i < (checksum_offset + sizeof(uint32_t)))) continue;
        sum = (sum << 5) ^ (sum >> 27) ^ p[i];
    }
    return sum;
}

// 计算 Config V5 结构的存储校验值。基于结构长度和 checksum 偏移复用统一算法，避免把校验字段自身计入。
static uint32_t config_checksum(const PatrolRemoteConfigV5_t* cfg)
{
    return config_checksum_bytes(cfg, sizeof(*cfg), offsetof(PatrolRemoteConfigV5_t, checksum));
}

// 检查正式 Config V5 的格式与校验是否有效。核验 magic、版本、长度、受限标志和 checksum；不接受旧版配置进入正式运行路径。
static uint8_t config_valid(const PatrolRemoteConfigV5_t* cfg)
{
    if (cfg == 0) return 0U;
    if ((cfg->magic != PATROL_CONFIG_MAGIC) || (cfg->version != PATROL_CONFIG_VERSION) ||
        (cfg->length != sizeof(*cfg)) || (cfg->reserved_remote > 1U) ||
        (cfg->mqtt_tls_enabled > 1U) || (cfg->diag_log_level > 3U)) return 0U;
    return (cfg->checksum == config_checksum(cfg)) ? 1U : 0U;
}

/* Tail journal occupies only unused bytes of the existing 0x0803F800 page.
 * Append-only writes cannot erase vehicle configuration. Full journal is
 * reported, never silently erased. A normal configuration save compacts it. */
#define MOTION_JOURNAL_ADDR (OTA_INFO_ADDR + 1024U)
#define MOTION_JOURNAL_SLOTS 8U
_Static_assert(sizeof(OTA_Info_t) <= 1024U, "configuration overlaps fault journal");
#define CONFIG_RECOVERY_MAGIC 0x43524631U /* CRF1 */
#define CONFIG_RECOVERY_COMMIT 0x434F4D31U /* COM1 */

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    OTA_Info_t info;
    uint8_t fault_valid;
    uint8_t reserved[3];
    MotionFaultRecord fault;
    uint32_t checksum;
    uint32_t commit;
} ConfigRecoveryRecord;
/* V1 recovery journal predates the optional secret extension. */
typedef struct {
    uint32_t magic;
    uint16_t version, length;
    uint8_t info[offsetof(OTA_Info_t, mqtt_tls_secret)];
    uint8_t fault_valid, reserved[3];
    MotionFaultRecord fault;
    uint32_t checksum, commit;
} LegacyConfigRecoveryRecord;


_Static_assert(sizeof(ConfigRecoveryRecord) <= CONFIG_RECOVERY_PAGE_SIZE,
               "config recovery record must fit its dedicated page");

// 计算运动故障记录的 CRC32。覆盖 checksum 之前的全部字节，用于识别掉电或部分写入导致的记录损坏。
static uint32_t fault_checksum(const MotionFaultRecord* r)
{
    const uint8_t* p=(const uint8_t*)r; uint32_t crc=0xFFFFFFFFU;
    for (uint32_t i=0;i<offsetof(MotionFaultRecord,checksum);i++) {
        crc^=p[i]; for (uint8_t b=0;b<8;b++) crc=(crc>>1)^((crc&1U)?0xEDB88320U:0U);
    }
    return crc^0xFFFFFFFFU;
}

// 计算当前配置恢复记录的 CRC32。覆盖提交结构中校验字段之前的数据，配合 commit 标志识别完整恢复快照。
static uint32_t recovery_checksum(const ConfigRecoveryRecord* record)
{
    const uint8_t* p = (const uint8_t*)record;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < offsetof(ConfigRecoveryRecord, checksum); i++) {
        crc ^= p[i];
        for (uint8_t b = 0U; b < 8U; b++) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

// 扫描故障日志槽并选出序号最新的有效记录。逐条检查 magic 与 CRC，损坏或空槽忽略；不自行擦页或触发配置恢复。
static uint8_t fault_load_direct(MotionFaultRecord* out)
{
    uint8_t found=0;
    if (out == 0) return 0U;
    for (uint32_t i=0;i<MOTION_JOURNAL_SLOTS;i++) {
        const MotionFaultRecord* r=(const MotionFaultRecord*)(MOTION_JOURNAL_ADDR+i*sizeof(*r));
        if (r->magic==0x4D463031U && r->checksum==fault_checksum(r) &&
            (!found || (int32_t)(r->sequence-out->sequence)>0)) { *out=*r; found=1; }
    }
    return found;
}

// 擦除给定地址所属的一页内部 Flash。调用方负责地址边界、写互斥和 Flash 解锁；只返回 HAL 擦除结果，不负责恢复原数据。
static uint8_t flash_erase_page(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = address;
    erase.NbPages = 1U;
    return HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK ? 1U : 0U;
}

// 逐字编程 Flash 并复读比较目标内容。调用方保证地址、源数据对齐及向上取整后的可读范围，且已完成擦除、解锁与写入互斥。
static uint8_t flash_program_words(uint32_t address, const void* data, uint32_t size)
{
    const uint32_t* words = (const uint32_t*)data;
    for (uint32_t i = 0U; i < (size + 3U) / 4U; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i * 4U, words[i]) != HAL_OK) return 0U;
    }
    return memcmp((const void*)address, data, size) == 0 ? 1U : 0U;
}

// 验证当前版本配置恢复快照的完整性。同时检查标识、版本、长度、提交标志和 CRC，只有完整提交的快照才可恢复。
static uint8_t recovery_valid(const ConfigRecoveryRecord* record)
{
    return record->magic == CONFIG_RECOVERY_MAGIC && record->version == 2U &&
           record->length == sizeof(*record) && record->commit == CONFIG_RECOVERY_COMMIT &&
           record->checksum == recovery_checksum(record);
}
// 识别历史恢复日志封装是否完整可恢复。仅兼容恢复记录的旧封装，不表示允许旧 Config V3/V4 进入正式运行路径。
static uint8_t legacy_recovery_valid(const LegacyConfigRecoveryRecord* record)
{
    const uint8_t* p = (const uint8_t*)record;
    uint32_t crc = 0xFFFFFFFFU;
    if (record->magic != CONFIG_RECOVERY_MAGIC || record->version != 1U ||
        record->length != sizeof(*record) || record->commit != CONFIG_RECOVERY_COMMIT) return 0;
    for (uint32_t i = 0; i < offsetof(LegacyConfigRecoveryRecord, checksum); ++i) {
        crc ^= p[i];
        for (uint8_t b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return record->checksum == (crc ^ 0xFFFFFFFFU);
}

// 擦除配置恢复页以清除已处理的恢复记录。调用者须已获得存储写入保护并解锁 Flash；只能在原数据恢复并验证后清理。
static uint8_t recovery_clear_unlocked(void)
{
    return flash_erase_page(CONFIG_RECOVERY_ADDR);
}

// 发现有效恢复日志时还原配置与最近故障。先擦写配置页、复读验证，成功后再清恢复页；失败保留恢复证据供后续重试，绝不修改 Boot State 页。
static uint8_t recovery_restore_if_needed(void)
{
    const ConfigRecoveryRecord* record = (const ConfigRecoveryRecord*)CONFIG_RECOVERY_ADDR;
    const LegacyConfigRecoveryRecord* legacy = (const LegacyConfigRecoveryRecord*)record;
    ConfigRecoveryRecord copy;
    if (recovery_valid(record)) copy = *record;
    else if (legacy_recovery_valid(legacy)) {
        memset(&copy, 0xFF, sizeof(copy));
        memcpy(&copy.info, legacy->info, sizeof(legacy->info));
        copy.fault_valid = legacy->fault_valid;
        copy.fault = legacy->fault;
    } else return 1U;
    HAL_FLASH_Unlock();
    if (!flash_erase_page(OTA_INFO_ADDR) ||
        !flash_program_words(OTA_INFO_ADDR, &copy.info, sizeof(copy.info)) ||
        (copy.fault_valid && !flash_program_words(MOTION_JOURNAL_ADDR, &copy.fault, sizeof(copy.fault)))) {
        HAL_FLASH_Lock();
        return 0U;
    }
    if (!recovery_clear_unlocked()) { HAL_FLASH_Lock(); return 0U; }
    HAL_FLASH_Lock();
    return 1U;
}

// 在必要恢复后读取最新有效运动故障。写入忙时避免递归恢复，找不到有效记录或恢复失败返回零，不能用空内容代替有效证据。
uint8_t OTA_Info_LoadMotionFault(MotionFaultRecord* out)
{
    if (!info_write_busy && !recovery_restore_if_needed()) return 0U;
    return fault_load_direct(out);
}
// 向故障日志的第一个空槽追加记录。重新计算 CRC 并逐字写入复读，不覆盖已有槽；日志满或写入失败返回零。
static uint8_t fault_save_unlocked(const MotionFaultRecord* record)
{
    MotionFaultRecord copy=*record; copy.checksum=fault_checksum(&copy);
    if (copy.magic!=0x4D463031U) return 0;
    for (uint32_t i=0;i<MOTION_JOURNAL_SLOTS;i++) {
        uint32_t address=MOTION_JOURNAL_ADDR+i*sizeof(copy);
        const uint32_t* old=(const uint32_t*)address; uint8_t empty=1;
        for (uint32_t j=0;j<sizeof(copy)/4;j++) if (old[j]!=0xFFFFFFFFU) empty=0;
        if (!empty) continue;
        HAL_FLASH_Unlock();
        for (uint32_t j=0;j<sizeof(copy)/4;j++) {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,address+4*j,((uint32_t*)&copy)[j])!=HAL_OK) {
                HAL_FLASH_Lock(); return 0;
            }
        }
        HAL_FLASH_Lock();
        return memcmp((const void*)address,&copy,sizeof(copy))==0;
    }
    return 0;
}

// 用恢复日志保护配置页的整页更新。先保存旧配置及故障并提交恢复记录，再逐字写新页、保留故障并清恢复页；新配置的整体复读由上层保存接口负责。
static uint8_t ota_info_write_raw_unlocked(const OTA_Info_t* info)
{
    MotionFaultRecord saved_fault;
    ConfigRecoveryRecord recovery;
    uint8_t have_fault;
    HAL_StatusTypeDef status;
    const uint32_t* words;
    uint32_t address = OTA_INFO_ADDR;
    uint32_t count;
    uint32_t i;

    if (info == 0) return 0U;
    if (!recovery_restore_if_needed()) return 0U;
    have_fault = fault_load_direct(&saved_fault);

    memset(&recovery, 0xFF, sizeof(recovery));
    recovery.magic = CONFIG_RECOVERY_MAGIC;
    recovery.version = 2U;
    recovery.length = sizeof(recovery);
    memcpy(&recovery.info, (const void*)OTA_INFO_ADDR, sizeof(recovery.info));
    recovery.fault_valid = have_fault;
    if (have_fault) recovery.fault = saved_fault;
    recovery.checksum = recovery_checksum(&recovery);
    recovery.commit = 0xFFFFFFFFU;

    HAL_FLASH_Unlock();
    if (!recovery_clear_unlocked() ||
        !flash_program_words(CONFIG_RECOVERY_ADDR, &recovery, offsetof(ConfigRecoveryRecord, commit)) ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          CONFIG_RECOVERY_ADDR + offsetof(ConfigRecoveryRecord, commit),
                          CONFIG_RECOVERY_COMMIT) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0U;
    }
    recovery.commit = CONFIG_RECOVERY_COMMIT;
    if (!recovery_valid((const ConfigRecoveryRecord*)CONFIG_RECOVERY_ADDR)) {
        HAL_FLASH_Lock();
        return 0U;
    }

    words = (const uint32_t*)info;
    count = (sizeof(*info) + 3U) / 4U;

    if (!flash_erase_page(OTA_INFO_ADDR))
    {
        HAL_FLASH_Lock();
        return 0U;
    }
    for (i = 0U; i < count; i++)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, words[i]);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0U;
        }
        address += 4U;
    }
    if (have_fault && !flash_program_words(MOTION_JOURNAL_ADDR, &saved_fault, sizeof(saved_fault))) {
        HAL_FLASH_Lock();
        return 0U;
    }
    if (!recovery_clear_unlocked()) { HAL_FLASH_Lock(); return 0U; }
    HAL_FLASH_Lock();
    return 1U;
}

// 以原子忙标志串行追加一条运动故障记录。必要时先恢复配置日志，再写空槽并复读；输出关闭和 OTA 空闲条件由运行层调用方保证，本函数不检查这些门禁。
uint8_t OTA_Info_SaveMotionFault(const MotionFaultRecord* record)
{
    if (__atomic_test_and_set(&info_write_busy,__ATOMIC_ACQUIRE)) return 0;
    uint8_t result=recovery_restore_if_needed() && fault_save_unlocked(record);
    __atomic_clear(&info_write_busy,__ATOMIC_RELEASE); return result;
}
// 以原子忙标志防止配置页写入重入。取得互斥后调用内部页更新并释放忙标志；运动、输出和 OTA 安全时机必须由调用方保证，本函数不检查这些状态。
static uint8_t ota_info_write_raw(const OTA_Info_t* info)
{
    if (__atomic_test_and_set(&info_write_busy,__ATOMIC_ACQUIRE)) return 0;
    uint8_t result=ota_info_write_raw_unlocked(info);
    __atomic_clear(&info_write_busy,__ATOMIC_RELEASE); return result;
}

// 读取 OTA_Info_t 原始存储结构。必要时先尝试恢复日志；本函数不验证整个结构或各业务字段，调用者须使用相应有效性检查。
void OTA_Info_Load(OTA_Info_t* info) { ota_info_read_raw(info); }
// 将 OTA 信息结构保存到共享配置页。复用受保护的页更新流程，不可在不满足存储安全前提的上下文调用。
uint8_t OTA_Info_Save(const OTA_Info_t* info) { return ota_info_write_raw(info); }

// 将 OTA_Info_t 全部置为擦除值并写回配置页。会同时清除该结构内的 Config V5、TLS 凭据和 OTA 字段，不能当作仅清 OTA 请求；不擦 Boot State 或整片 Flash。
uint8_t OTA_Info_Clear(void)
{
    OTA_Info_t info;
    memset(&info, 0xFF, sizeof(info));
    return ota_info_write_raw(&info);
}

// 从配置页载入校验通过的 Config V5。仅在格式与校验均合法时复制到 cfg，失败由上层决定使用未标定默认值。
uint8_t OTA_Info_LoadRemoteConfig(PatrolRemoteConfigV5_t* cfg)
{
    OTA_Info_t info;
    if (cfg == 0) return 0U;
    ota_info_read_raw(&info);
    if (config_valid(&info.remote_config))
    {
        *cfg = info.remote_config;
        return 1U;
    }
    return 0U;
}

// 保存有效 Config V5 并复读核对内容。保留 OTA 信息其他字段，通过恢复日志保护更新，写后再次验证格式和逐字节一致性。
uint8_t OTA_Info_SaveRemoteConfig(const PatrolRemoteConfigV5_t* cfg)
{
    OTA_Info_t info;
    if ((cfg == 0) || !config_valid(cfg)) return 0U;
    ota_info_read_raw(&info);
    info.remote_config = *cfg;
    if (!ota_info_write_raw(&info)) return 0U;
    ota_info_read_raw(&info);
    return config_valid(&info.remote_config) && memcmp(cfg, &info.remote_config, sizeof(*cfg)) == 0;
}

// 检查 OTA URL 的协议、长度与可打印字符。接受代码规定的 HTTP/HTTPS 前缀，必须在固定缓冲范围内找到终止符；更严格的来源策略由上层检查。
static uint8_t ota_url_valid(const char* url)
{
    uint32_t i;
    if ((url == 0) || (url[0] == '\0') || ((uint8_t)url[0] == 0xFFU)) return 0U;
    if ((strncmp(url, "http://", 7U) != 0) && (strncmp(url, "https://", 8U) != 0)) return 0U;
    for (i = 0U; i < 128U; i++)
    {
        uint8_t c = (uint8_t)url[i];
        if (c == '\0') return 1U;
        if ((c < 0x20U) || (c > 0x7EU)) return 0U;
    }
    return 0U;
}

// 检查兼容 OTA 请求标志及 URL 基本格式。仅说明存储请求结构可解释，不代表镜像尺寸、来源或 CRC 已通过安装校验。
uint8_t OTA_Info_IsRequestValid(const OTA_Info_t* info)
{
    return ((info != 0) && (info->ota_req_magic == OTA_REQ_MAGIC) && ota_url_valid(info->url)) ? 1U : 0U;
}

// 读取配置页并查询是否保留有效 OTA 请求。使用统一请求校验，不直接启动下载或复位。
uint8_t OTA_Info_HasRequest(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return OTA_Info_IsRequestValid(&info);
}

// 查询兼容 App 有效标志是否存在。该标志与 Boot State 的试运行确认和现场验收不是同一个概念。
uint8_t OTA_Info_AppMarkedValid(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return (info.app_valid_magic == APP_VALID_MAGIC) ? 1U : 0U;
}

// 设置兼容 App 有效标志并请求保存。复用配置页写入，本接口无返回值且忽略保存结果，调用方不能据调用完成宣称落盘成功。
void OTA_Info_MarkAppValid(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    if ((info.ota_req_magic == 0xFFFFFFFFU) || (info.ota_req_magic == 0x00000000U)) info.ota_req_magic = 0U;
    info.app_valid_magic = APP_VALID_MAGIC;
    (void)ota_info_write_raw(&info);
}

// 清除兼容 App 有效标志并请求保存。仅修改该兼容字段，底层写失败不会经本接口返回。
void OTA_Info_ClearAppValid(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.app_valid_magic = 0U;
    (void)ota_info_write_raw(&info);
}

// 登记兼容 OTA 镜像大小、CRC 与下载 URL。有界复制 URL、重置失败计数，无效请求清除标志；接口忽略底层保存结果，不能作为升级成功证明。
void OTA_Info_SetRequest(uint32_t fw_size, uint32_t fw_crc32, const char* url)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.ota_req_magic = OTA_REQ_MAGIC;
    info.fw_size = fw_size;
    info.fw_crc32 = fw_crc32;
    info.req_fail_count = 0U;
    memset(info.url, 0, sizeof(info.url));
    if (url != 0) strncpy(info.url, url, sizeof(info.url) - 1U);
    info.url[sizeof(info.url) - 1U] = '\0';
    if (!OTA_Info_IsRequestValid(&info)) info.ota_req_magic = 0U;
    (void)ota_info_write_raw(&info);
}

// 清除兼容 OTA 请求及失败计数。保留无关配置字段，页更新仍通过统一存储流程，接口不返回保存是否成功。
void OTA_Info_ClearRequest(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.ota_req_magic = 0U;
    info.req_fail_count = 0U;
    memset(info.url, 0, sizeof(info.url));
    (void)ota_info_write_raw(&info);
}

// 读取有效兼容 OTA 请求的失败计数。没有有效请求时返回零，计数不等同于 Boot State 的试运行失败次数。
uint32_t OTA_Info_GetReqFailCount(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return OTA_Info_IsRequestValid(&info) ? info.req_fail_count : 0U;
}

// 将兼容 OTA 请求失败计数清零并请求保存。不清除其他升级状态，底层写入结果未通过本接口返回。
void OTA_Info_ClearReqFailCount(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.req_fail_count = 0U;
    (void)ota_info_write_raw(&info);
}

// 对有效兼容 OTA 请求的失败计数作饱和递增。无有效请求时不写，达到最大值后不回绕，保存结果由当前兼容接口忽略。
void OTA_Info_IncReqFailCount(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    if (!OTA_Info_IsRequestValid(&info)) return;
    if (info.req_fail_count < 0xFFFFFFFFU) info.req_fail_count++;
    (void)ota_info_write_raw(&info);
}

// 从配置页解码 TLS 凭据记录。复用平台密钥记录格式校验，调用者不得将凭据输出到普通日志或公共证据文件。
uint8_t OTA_Info_LoadMqttCredentials(ABoxTlsCredentials* credentials)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return (uint8_t)ABoxTlsSecretRecord_Decode(&info.mqtt_tls_secret, credentials);
}

// 将 Config V5 与 TLS 凭据作为一个快照保存。先校验并编码凭据，再执行受恢复日志保护的页更新，最后同时复读比较配置与密钥记录。
uint8_t OTA_Info_SaveMqttSnapshot(const PatrolRemoteConfigV5_t* cfg, const ABoxTlsCredentials* credentials)
{
    OTA_Info_t info;
    ABoxTlsSecretRecord expected;
    if (!cfg || !config_valid(cfg) || !ABoxTlsCredentials_Valid(credentials)) return 0;
    ABoxTlsSecretRecord_Encode(&expected, credentials);
    ota_info_read_raw(&info);
    info.remote_config = *cfg;
    info.mqtt_tls_secret = expected;
    if (!ota_info_write_raw(&info)) return 0;
    ota_info_read_raw(&info);
    return memcmp(&info.remote_config, cfg, sizeof(*cfg)) == 0 &&
           memcmp(&info.mqtt_tls_secret, &expected, sizeof(expected)) == 0;
}
