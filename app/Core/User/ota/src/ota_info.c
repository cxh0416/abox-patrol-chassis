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

static uint8_t recovery_restore_if_needed(void);
static uint8_t info_write_busy;

static void ota_info_read_raw(OTA_Info_t* info)
{
    if (!info_write_busy) (void)recovery_restore_if_needed();
    if (info != 0) memcpy(info, (const void*)OTA_INFO_ADDR, sizeof(*info));
}

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

static uint32_t config_checksum(const PatrolRemoteConfigV5_t* cfg)
{
    return config_checksum_bytes(cfg, sizeof(*cfg), offsetof(PatrolRemoteConfigV5_t, checksum));
}

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

_Static_assert(sizeof(ConfigRecoveryRecord) <= CONFIG_RECOVERY_PAGE_SIZE,
               "config recovery record must fit its dedicated page");

static uint32_t fault_checksum(const MotionFaultRecord* r)
{
    const uint8_t* p=(const uint8_t*)r; uint32_t crc=0xFFFFFFFFU;
    for (uint32_t i=0;i<offsetof(MotionFaultRecord,checksum);i++) {
        crc^=p[i]; for (uint8_t b=0;b<8;b++) crc=(crc>>1)^((crc&1U)?0xEDB88320U:0U);
    }
    return crc^0xFFFFFFFFU;
}

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

static uint8_t flash_erase_page(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = address;
    erase.NbPages = 1U;
    return HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK ? 1U : 0U;
}

static uint8_t flash_program_words(uint32_t address, const void* data, uint32_t size)
{
    const uint32_t* words = (const uint32_t*)data;
    for (uint32_t i = 0U; i < (size + 3U) / 4U; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i * 4U, words[i]) != HAL_OK) return 0U;
    }
    return memcmp((const void*)address, data, size) == 0 ? 1U : 0U;
}

static uint8_t recovery_valid(const ConfigRecoveryRecord* record)
{
    return record->magic == CONFIG_RECOVERY_MAGIC && record->version == 1U &&
           record->length == sizeof(*record) && record->commit == CONFIG_RECOVERY_COMMIT &&
           record->checksum == recovery_checksum(record);
}

static uint8_t recovery_clear_unlocked(void)
{
    return flash_erase_page(CONFIG_RECOVERY_ADDR);
}

static uint8_t recovery_restore_if_needed(void)
{
    const ConfigRecoveryRecord* record = (const ConfigRecoveryRecord*)CONFIG_RECOVERY_ADDR;
    if (!recovery_valid(record)) return 1U;
    ConfigRecoveryRecord copy = *record;
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

uint8_t OTA_Info_LoadMotionFault(MotionFaultRecord* out)
{
    if (!info_write_busy && !recovery_restore_if_needed()) return 0U;
    return fault_load_direct(out);
}
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
    recovery.version = 1U;
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

uint8_t OTA_Info_SaveMotionFault(const MotionFaultRecord* record)
{
    if (__atomic_test_and_set(&info_write_busy,__ATOMIC_ACQUIRE)) return 0;
    uint8_t result=recovery_restore_if_needed() && fault_save_unlocked(record);
    __atomic_clear(&info_write_busy,__ATOMIC_RELEASE); return result;
}
static uint8_t ota_info_write_raw(const OTA_Info_t* info)
{
    if (__atomic_test_and_set(&info_write_busy,__ATOMIC_ACQUIRE)) return 0;
    uint8_t result=ota_info_write_raw_unlocked(info);
    __atomic_clear(&info_write_busy,__ATOMIC_RELEASE); return result;
}

void OTA_Info_Load(OTA_Info_t* info) { ota_info_read_raw(info); }
uint8_t OTA_Info_Save(const OTA_Info_t* info) { return ota_info_write_raw(info); }

uint8_t OTA_Info_Clear(void)
{
    OTA_Info_t info;
    memset(&info, 0xFF, sizeof(info));
    return ota_info_write_raw(&info);
}

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

uint8_t OTA_Info_IsRequestValid(const OTA_Info_t* info)
{
    return ((info != 0) && (info->ota_req_magic == OTA_REQ_MAGIC) && ota_url_valid(info->url)) ? 1U : 0U;
}

uint8_t OTA_Info_HasRequest(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return OTA_Info_IsRequestValid(&info);
}

uint8_t OTA_Info_AppMarkedValid(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return (info.app_valid_magic == APP_VALID_MAGIC) ? 1U : 0U;
}

void OTA_Info_MarkAppValid(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    if ((info.ota_req_magic == 0xFFFFFFFFU) || (info.ota_req_magic == 0x00000000U)) info.ota_req_magic = 0U;
    info.app_valid_magic = APP_VALID_MAGIC;
    (void)ota_info_write_raw(&info);
}

void OTA_Info_ClearAppValid(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.app_valid_magic = 0U;
    (void)ota_info_write_raw(&info);
}

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

void OTA_Info_ClearRequest(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.ota_req_magic = 0U;
    info.req_fail_count = 0U;
    memset(info.url, 0, sizeof(info.url));
    (void)ota_info_write_raw(&info);
}

uint32_t OTA_Info_GetReqFailCount(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    return OTA_Info_IsRequestValid(&info) ? info.req_fail_count : 0U;
}

void OTA_Info_ClearReqFailCount(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    info.req_fail_count = 0U;
    (void)ota_info_write_raw(&info);
}

void OTA_Info_IncReqFailCount(void)
{
    OTA_Info_t info;
    ota_info_read_raw(&info);
    if (!OTA_Info_IsRequestValid(&info)) return;
    if (info.req_fail_count < 0xFFFFFFFFU) info.req_fail_count++;
    (void)ota_info_write_raw(&info);
}
