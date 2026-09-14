#include "ota_info.h"

#include <stddef.h>
#include <string.h>

#include "main.h"

_Static_assert(sizeof(OTA_Info_t) <= OTA_INFO_PAGE_SIZE, "OTA info must fit in one flash page");
_Static_assert(sizeof(VcuRemoteConfig_t) == 396U, "VCU v4 config layout changed");

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t checksum;
    uint8_t remote_enabled;
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
} VcuRemoteConfigV3_t;

_Static_assert(sizeof(VcuRemoteConfigV3_t) == 392U, "VCU v3 config layout changed");

static void ota_info_read_raw(OTA_Info_t* info)
{
    if (info != 0) memcpy(info, (const void*)OTA_INFO_ADDR, sizeof(*info));
}

static uint32_t config_checksum(const VcuRemoteConfig_t* cfg)
{
    const uint8_t* p = (const uint8_t*)cfg;
    uint32_t sum = 0x13572468U;
    uint16_t i;

    if (cfg == 0) return 0U;
    for (i = 0U; i < sizeof(*cfg); i++)
    {
        if ((i >= offsetof(VcuRemoteConfig_t, checksum)) &&
            (i < (offsetof(VcuRemoteConfig_t, checksum) + sizeof(cfg->checksum)))) continue;
        sum = (sum << 5) ^ (sum >> 27) ^ p[i];
    }
    return sum;
}

static uint32_t config_v3_checksum(const VcuRemoteConfigV3_t* cfg)
{
    const uint8_t* p = (const uint8_t*)cfg;
    uint32_t sum = 0x13572468U;
    uint16_t i;

    if (cfg == 0) return 0U;
    for (i = 0U; i < sizeof(*cfg); i++)
    {
        if ((i >= offsetof(VcuRemoteConfigV3_t, checksum)) &&
            (i < (offsetof(VcuRemoteConfigV3_t, checksum) + sizeof(cfg->checksum)))) continue;
        sum = (sum << 5) ^ (sum >> 27) ^ p[i];
    }
    return sum;
}

static uint8_t config_valid(const VcuRemoteConfig_t* cfg)
{
    if (cfg == 0) return 0U;
    if ((cfg->magic != VCU_CONFIG_MAGIC) || (cfg->version != VCU_CONFIG_VERSION) ||
        (cfg->length != sizeof(*cfg)) || (cfg->remote_enabled > 1U) || (cfg->diag_log_level > 3U)) return 0U;
    return (cfg->checksum == config_checksum(cfg)) ? 1U : 0U;
}

static uint8_t config_v3_valid(const VcuRemoteConfigV3_t* cfg)
{
    if (cfg == 0) return 0U;
    if ((cfg->magic != VCU_CONFIG_MAGIC) || (cfg->version != 3U) ||
        (cfg->length != sizeof(*cfg)) || (cfg->remote_enabled > 1U) ||
        (cfg->diag_log_level > 3U)) return 0U;
    return (cfg->checksum == config_v3_checksum(cfg)) ? 1U : 0U;
}

static uint8_t ota_info_write_raw(const OTA_Info_t* info)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    const uint32_t* words;
    uint32_t address = OTA_INFO_ADDR;
    uint32_t count;
    uint32_t i;

    if (info == 0) return 0U;
    words = (const uint32_t*)info;
    count = (sizeof(*info) + 3U) / 4U;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = OTA_INFO_ADDR;
    erase.NbPages = 1U;
    status = HAL_FLASHEx_Erase(&erase, &page_error);
    if (status != HAL_OK)
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
    HAL_FLASH_Lock();
    return 1U;
}

void OTA_Info_Load(OTA_Info_t* info) { ota_info_read_raw(info); }
uint8_t OTA_Info_Save(const OTA_Info_t* info) { return ota_info_write_raw(info); }

uint8_t OTA_Info_Clear(void)
{
    OTA_Info_t info;
    memset(&info, 0xFF, sizeof(info));
    return ota_info_write_raw(&info);
}

uint8_t OTA_Info_LoadRemoteConfig(VcuRemoteConfig_t* cfg)
{
    OTA_Info_t info;
    const VcuRemoteConfigV3_t* legacy;
    if (cfg == 0) return 0U;
    ota_info_read_raw(&info);
    if (config_valid(&info.remote_config))
    {
        *cfg = info.remote_config;
        return 1U;
    }
    legacy = (const VcuRemoteConfigV3_t*)&info.remote_config;
    if (!config_v3_valid(legacy)) return 0U;
    memset(cfg, 0, sizeof(*cfg));
    memcpy(cfg, legacy, sizeof(*legacy));
    cfg->version = VCU_CONFIG_VERSION;
    cfg->length = sizeof(*cfg);
    cfg->steering_counts_per_degree_milli = 0U;
    /* V3 angles represented the steering shaft. Require a new wheel-angle calibration. */
    cfg->remote_enabled = 0U;
    cfg->calibrated = 0U;
    cfg->checksum = config_checksum(cfg);
    return 1U;
}

uint8_t OTA_Info_SaveRemoteConfig(const VcuRemoteConfig_t* cfg)
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
