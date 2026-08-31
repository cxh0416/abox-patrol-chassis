#include "vcu_config.h"

#include <string.h>

#include "app_log.h"

static VcuRemoteConfig_t g_vcu_config;

static void copy_string(char* dst, uint16_t size, const char* src)
{
    if ((dst == 0) || (size == 0U)) return;
    if (src == 0)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

static uint32_t config_checksum(const VcuRemoteConfig_t* cfg)
{
    const uint8_t* bytes = (const uint8_t*)cfg;
    uint32_t sum = 0x13572468U;
    uint16_t i;

    for (i = 0U; i < sizeof(*cfg); i++)
    {
        if ((i >= 8U) && (i < 12U)) continue;
        sum = (sum << 5) ^ (sum >> 27) ^ bytes[i];
    }
    return sum;
}

static uint8_t string_is_safe(const char* value, uint16_t size, uint8_t topic_component)
{
    uint16_t i;
    if ((value == 0) || (size == 0U) || (value[0] == '\0') || ((uint8_t)value[0] == 0xFFU)) return 0U;
    for (i = 0U; i < size; i++)
    {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return 1U;
        if ((ch < 0x21U) || (ch > 0x7EU) || (ch == '"') || (ch == '\\')) return 0U;
        if (topic_component && ((ch == '/') || (ch == '+') || (ch == '#'))) return 0U;
    }
    return 0U;
}

void VcuConfig_ResetDefault(VcuRemoteConfig_t* cfg)
{
    if (cfg == 0) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = VCU_CONFIG_MAGIC;
    cfg->version = VCU_CONFIG_VERSION;
    cfg->length = sizeof(*cfg);
    cfg->remote_enabled = 0U;
    copy_string(cfg->vid, sizeof(cfg->vid), VCU_CONFIG_DEFAULT_VID);
    copy_string(cfg->mqtt_host, sizeof(cfg->mqtt_host), VCU_CONFIG_DEFAULT_MQTT_HOST);
    cfg->mqtt_port = VCU_CONFIG_DEFAULT_MQTT_PORT;
    copy_string(cfg->mqtt_username, sizeof(cfg->mqtt_username), VCU_CONFIG_DEFAULT_MQTT_USER);
    copy_string(cfg->mqtt_password, sizeof(cfg->mqtt_password), VCU_CONFIG_DEFAULT_MQTT_PASS);
    copy_string(cfg->diag_log_host, sizeof(cfg->diag_log_host), VCU_CONFIG_DEFAULT_DIAG_HOST);
    cfg->diag_log_port = VCU_CONFIG_DEFAULT_DIAG_PORT;
    cfg->diag_log_level = VCU_CONFIG_DEFAULT_DIAG_LEVEL;
    copy_string(cfg->ota_origin, sizeof(cfg->ota_origin), VCU_CONFIG_DEFAULT_OTA_ORIGIN);
    cfg->steering_direction = 1;
    cfg->absolute_direction = 1;
    cfg->steering_tolerance_mdeg = 2000U;
    cfg->calibrated = 0U;
    cfg->checksum = config_checksum(cfg);
}

void VcuConfig_Load(void)
{
    if (!OTA_Info_LoadRemoteConfig(&g_vcu_config))
    {
        VcuConfig_ResetDefault(&g_vcu_config);
        APP_LOGW("CFG", "default VCU config; remote disabled");
    }
    else
    {
        APP_LOGI("CFG", "loaded vid=%s enabled=%u", g_vcu_config.vid,
                 (unsigned int)g_vcu_config.remote_enabled);
    }
}

const VcuRemoteConfig_t* VcuConfig_Get(void) { return &g_vcu_config; }

uint8_t VcuConfig_Save(const VcuRemoteConfig_t* cfg)
{
    VcuRemoteConfig_t next;
    if (cfg == 0) return 0U;
    next = *cfg;
    next.magic = VCU_CONFIG_MAGIC;
    next.version = VCU_CONFIG_VERSION;
    next.length = sizeof(next);
    next.remote_enabled = next.remote_enabled ? 1U : 0U;
    next.calibrated = next.calibrated ? 1U : 0U;
    if (next.diag_log_level > 3U) next.diag_log_level = VCU_CONFIG_DEFAULT_DIAG_LEVEL;
    if (next.mqtt_port == 0U) next.mqtt_port = VCU_CONFIG_DEFAULT_MQTT_PORT;
    if (next.diag_log_port == 0U) next.diag_log_port = VCU_CONFIG_DEFAULT_DIAG_PORT;
    next.checksum = config_checksum(&next);
    if (!OTA_Info_SaveRemoteConfig(&next)) return 0U;
    g_vcu_config = next;
    APP_LOGI("CFG", "saved enabled=%u vid=%s", (unsigned int)next.remote_enabled, next.vid);
    return 1U;
}

uint8_t VcuConfig_SetRemoteEnabled(uint8_t enabled)
{
    VcuRemoteConfig_t next = g_vcu_config;
    next.remote_enabled = enabled ? 1U : 0U;
    return VcuConfig_Save(&next);
}

uint8_t VcuConfig_SetMqtt(const char* vid, const char* host, uint16_t port, const char* username,
                          const char* password)
{
    VcuRemoteConfig_t next = g_vcu_config;
    if (vid != 0) copy_string(next.vid, sizeof(next.vid), vid);
    if (host != 0) copy_string(next.mqtt_host, sizeof(next.mqtt_host), host);
    if (port != 0U) next.mqtt_port = port;
    if (username != 0) copy_string(next.mqtt_username, sizeof(next.mqtt_username), username);
    if (password != 0) copy_string(next.mqtt_password, sizeof(next.mqtt_password), password);
    return VcuConfig_Save(&next);
}

uint8_t VcuConfig_SetDiagLog(uint8_t enabled, const char* host, uint16_t port, uint8_t level)
{
    VcuRemoteConfig_t next = g_vcu_config;
    next.diag_log_enabled = enabled ? 1U : 0U;
    if (host != 0) copy_string(next.diag_log_host, sizeof(next.diag_log_host), host);
    if (port != 0U) next.diag_log_port = port;
    if (level <= 3U) next.diag_log_level = level;
    return VcuConfig_Save(&next);
}

uint8_t VcuConfig_IsCloudReady(const VcuRemoteConfig_t* cfg)
{
    if (cfg == 0) return 0U;
    if (!string_is_safe(cfg->vid, sizeof(cfg->vid), 1U)) return 0U;
    if (!string_is_safe(cfg->mqtt_host, sizeof(cfg->mqtt_host), 0U)) return 0U;
    if (!string_is_safe(cfg->mqtt_username, sizeof(cfg->mqtt_username), 0U)) return 0U;
    if (!string_is_safe(cfg->mqtt_password, sizeof(cfg->mqtt_password), 0U)) return 0U;
    return (cfg->mqtt_port != 0U) ? 1U : 0U;
}

uint8_t VcuConfig_IsOtaOriginValid(const VcuRemoteConfig_t* cfg)
{
    const char* origin;
    if (cfg == 0) return 0U;
    origin = cfg->ota_origin;
    return (strncmp(origin, "https://", 8U) == 0 && string_is_safe(origin, sizeof(cfg->ota_origin), 0U)) ? 1U : 0U;
}

uint8_t VcuConfig_SetCalibration(int32_t steering_center_count, uint32_t absolute_center_count,
                                 uint32_t absolute_counts_per_degree_milli,
                                 int8_t steering_direction, int8_t absolute_direction,
                                 uint16_t tolerance_mdeg)
{
    VcuRemoteConfig_t next = g_vcu_config;
    if (absolute_counts_per_degree_milli == 0U || tolerance_mdeg == 0U || tolerance_mdeg > 2000U ||
        (steering_direction != 1 && steering_direction != -1) ||
        (absolute_direction != 1 && absolute_direction != -1)) return 0U;
    next.steering_center_count = steering_center_count;
    next.absolute_center_count = absolute_center_count;
    next.absolute_counts_per_degree_milli = absolute_counts_per_degree_milli;
    next.steering_direction = steering_direction;
    next.absolute_direction = absolute_direction;
    next.steering_tolerance_mdeg = tolerance_mdeg;
    next.calibrated = 1U;
    return VcuConfig_Save(&next);
}
