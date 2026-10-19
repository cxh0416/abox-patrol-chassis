#include "patrol_config.h"

#include <string.h>

#include "app_log.h"
#include "vehicle_steering_profile.h"

/* >>>>> CAR-PROFILE: START >>>>> */
/*
 * Per-car calibration profiles for a shared A-BOX board that is moved
 * between chassis. PATROL_ACTIVE_CAR selects the car this image is built
 * for: 0 disables this per-car profile gate, 1..4 selects a car profile.
 * Bench/field images set 1..4 explicitly; formal images leave it 0.
 *
 * steering_center_count is the driver 6064 raw count at mechanical center.
 * absolute_center_count is the encoder zero (0 after zeroing).
 *
 * Use PATROL_CAR_CENTER_UNKNOWN until a car has been calibrated. After that
 * car's first center-calibration commit, fill in its measured
 * steering_center_count and rebuild the image for that car.
 *
 * When the loaded Config V5 does not match the active car's known profile,
 * only the runtime calibrated flag is cleared; Flash content is preserved so
 * the normal center-calibration flow can run and commit this car's values.
 *
 * CAR1 historical profile (do not delete):
 *   steering_center_count = -83815
 *   absolute_center_count = 0
 */
#ifndef PATROL_ACTIVE_CAR
#define PATROL_ACTIVE_CAR 0U
#endif

#define PATROL_CAR_CENTER_UNKNOWN ((int32_t)0x7FFFFFFF)

typedef struct {
    const char* tag;
    int32_t steering_center_count;
    uint32_t absolute_center_count;
} PatrolCarProfile;

static const PatrolCarProfile car_profiles[] = {
    { "CAR1", -83815, 0U },
    { "CAR2", -1463, 0U },  /* measured after CAR2 center calibration */
    { "CAR3", PATROL_CAR_CENTER_UNKNOWN, 0U },
    { "CAR4", PATROL_CAR_CENTER_UNKNOWN, 0U },
};

static const PatrolCarProfile* active_car_profile(void)
{
    if (PATROL_ACTIVE_CAR < 1U || PATROL_ACTIVE_CAR > 4U) return 0;
    return &car_profiles[PATROL_ACTIVE_CAR - 1U];
}

static uint8_t config_matches_active_car(const PatrolRemoteConfigV5_t* cfg)
{
    const PatrolCarProfile* profile = active_car_profile();
    if (cfg == 0 || profile == 0 || !cfg->calibrated) return 0U;
    if (profile->steering_center_count == PATROL_CAR_CENTER_UNKNOWN) return 0U;
    return (uint8_t)(cfg->steering_center_count == profile->steering_center_count &&
        cfg->absolute_center_count == profile->absolute_center_count);
}
/* <<<<< CAR-PROFILE: END <<<<< */

static PatrolRemoteConfigV5_t g_patrol_config;
static ABoxTlsCredentials g_mqtt_credentials;

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

static uint32_t config_checksum(const PatrolRemoteConfigV5_t* cfg)
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
        if (topic_component && !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return 0U;
    }
    return 0U;
}

void PatrolConfig_ResetDefault(PatrolRemoteConfigV5_t* cfg)
{
    if (cfg == 0) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = PATROL_CONFIG_MAGIC;
    cfg->version = PATROL_CONFIG_VERSION;
    cfg->length = sizeof(*cfg);
    cfg->reserved_remote = 0U;
    cfg->mqtt_tls_enabled = 1U;
    cfg->mqtt_tls_profile_id = PATROL_CONFIG_TLS_PROFILE_DEFAULT;
    copy_string(cfg->vid, sizeof(cfg->vid), PATROL_CONFIG_DEFAULT_VID);
    copy_string(cfg->mqtt_host, sizeof(cfg->mqtt_host), PATROL_CONFIG_DEFAULT_MQTT_HOST);
    cfg->mqtt_port = PATROL_CONFIG_DEFAULT_MQTT_PORT;
    copy_string(cfg->mqtt_username, sizeof(cfg->mqtt_username), PATROL_CONFIG_DEFAULT_MQTT_USER);
    copy_string(cfg->mqtt_password, sizeof(cfg->mqtt_password), PATROL_CONFIG_DEFAULT_MQTT_PASS);
    copy_string(cfg->diag_log_host, sizeof(cfg->diag_log_host), PATROL_CONFIG_DEFAULT_DIAG_HOST);
    cfg->diag_log_port = PATROL_CONFIG_DEFAULT_DIAG_PORT;
    cfg->diag_log_level = PATROL_CONFIG_DEFAULT_DIAG_LEVEL;
    copy_string(cfg->ota_origin, sizeof(cfg->ota_origin), PATROL_CONFIG_DEFAULT_OTA_ORIGIN);
    cfg->steering_direction = 1;
    cfg->absolute_direction = 1;
    cfg->steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
    cfg->absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    cfg->steering_tolerance_mdeg = 2000U;
    cfg->calibrated = 0U;
    cfg->checksum = config_checksum(cfg);
}

void PatrolConfig_Load(void)
{
    (void)OTA_Info_LoadMqttCredentials(&g_mqtt_credentials);
    if (!OTA_Info_LoadRemoteConfig(&g_patrol_config))
    {
        PatrolConfig_ResetDefault(&g_patrol_config);
        APP_LOGW("CFG", "default chassis config; calibration required");
    }
    else
    {
        APP_LOGI("CFG", "loaded vid=%s", g_patrol_config.vid);
/* >>>>> CAR-PROFILE: START >>>>> */
        if (PATROL_ACTIVE_CAR >= 1U && PATROL_ACTIVE_CAR <= 4U &&
            g_patrol_config.calibrated && !config_matches_active_car(&g_patrol_config))
        {
            /* The board is on a different car, or this car has not been
             * calibrated yet. Clear only the runtime flag; Flash is kept.
             * A center-calibration commit persists this car's values. */
            g_patrol_config.calibrated = 0U;
            APP_LOGW("CFG", "%s calibration profile not ready; center calibration required",
                active_car_profile() ? active_car_profile()->tag : "ACTIVE_CAR");
        }
/* <<<<< CAR-PROFILE: END <<<<< */
    }
}

const PatrolRemoteConfigV5_t* PatrolConfig_Get(void) { return &g_patrol_config; }

uint8_t PatrolConfig_IsVidValid(const char* vid)
{ return string_is_safe(vid, sizeof(g_patrol_config.vid), 1U); }

static uint8_t prepare_snapshot(const PatrolRemoteConfigV5_t* cfg, PatrolRemoteConfigV5_t* output)
{
    PatrolRemoteConfigV5_t next;
    if (cfg == 0) return 0U;
    next = *cfg;
    if (!PatrolConfig_IsVidValid(next.vid)) return 0U;
    next.magic = PATROL_CONFIG_MAGIC;
    next.version = PATROL_CONFIG_VERSION;
    next.length = sizeof(next);
    next.reserved_remote = 0U;
    next.mqtt_tls_enabled = next.mqtt_tls_enabled ? 1U : 0U;
    next.calibrated = next.calibrated ? 1U : 0U;
    if (next.calibrated && (next.steering_counts_per_degree_milli == 0U ||
                            next.absolute_counts_per_degree_milli == 0U ||
                            (next.steering_direction != 1 && next.steering_direction != -1) ||
                            (next.absolute_direction != 1 && next.absolute_direction != -1) ||
                            next.steering_tolerance_mdeg == 0U || next.steering_tolerance_mdeg > 2000U))
        return 0U;
    if (next.diag_log_level > 3U) next.diag_log_level = PATROL_CONFIG_DEFAULT_DIAG_LEVEL;
    if (next.diag_log_port == 0U) next.diag_log_port = PATROL_CONFIG_DEFAULT_DIAG_PORT;
    next.checksum = config_checksum(&next);
    *output = next;
    return 1U;
}

const ABoxTlsCredentials* PatrolConfig_MqttCredentials(void) { return &g_mqtt_credentials; }
uint8_t PatrolConfig_WriteMqttSnapshot(const PatrolRemoteConfigV5_t* cfg, const ABoxTlsCredentials* credentials)
{
    PatrolRemoteConfigV5_t next;
    return prepare_snapshot(cfg, &next) && OTA_Info_SaveMqttSnapshot(&next, credentials);
}
uint8_t PatrolConfig_Save(const PatrolRemoteConfigV5_t* cfg)
{
    PatrolRemoteConfigV5_t next;
    if (!prepare_snapshot(cfg, &next) || !OTA_Info_SaveRemoteConfig(&next)) return 0U;
    g_patrol_config = next;
    APP_LOGI("CFG", "saved vid=%s", next.vid);
    return 1U;
}

uint8_t PatrolConfig_SetMqtt(const char* vid, const char* host, uint16_t port, const char* username,
                          const char* password)
{
    PatrolRemoteConfigV5_t next = g_patrol_config;
    if (vid != 0 && !PatrolConfig_IsVidValid(vid)) return 0U;
    if (vid != 0) copy_string(next.vid, sizeof(next.vid), vid);
    if (host != 0) copy_string(next.mqtt_host, sizeof(next.mqtt_host), host);
    if (port != 0U) next.mqtt_port = port;
    if (username != 0) copy_string(next.mqtt_username, sizeof(next.mqtt_username), username);
    if (password != 0) copy_string(next.mqtt_password, sizeof(next.mqtt_password), password);
    return PatrolConfig_Save(&next);
}

uint8_t PatrolConfig_SetDiagLog(uint8_t enabled, const char* host, uint16_t port, uint8_t level)
{
    PatrolRemoteConfigV5_t next = g_patrol_config;
    next.diag_log_enabled = enabled ? 1U : 0U;
    if (host != 0) copy_string(next.diag_log_host, sizeof(next.diag_log_host), host);
    if (port != 0U) next.diag_log_port = port;
    if (level <= 3U) next.diag_log_level = level;
    return PatrolConfig_Save(&next);
}

uint8_t PatrolConfig_IsCloudReady(const PatrolRemoteConfigV5_t* cfg)
{
    if (cfg == 0) return 0U;
    if (!string_is_safe(cfg->vid, sizeof(cfg->vid), 1U)) return 0U;
    if (!string_is_safe(cfg->mqtt_host, sizeof(cfg->mqtt_host), 0U)) return 0U;
    if (!string_is_safe(cfg->mqtt_username, sizeof(cfg->mqtt_username), 0U)) return 0U;
    if (!string_is_safe(cfg->mqtt_password, sizeof(cfg->mqtt_password), 0U)) return 0U;
    return (cfg->mqtt_port != 0U) ? 1U : 0U;
}

uint8_t PatrolConfig_IsOtaOriginValid(const PatrolRemoteConfigV5_t* cfg)
{
    const char* origin;
    if (cfg == 0) return 0U;
    origin = cfg->ota_origin;
    return (strncmp(origin, "https://", 8U) == 0 && string_is_safe(origin, sizeof(cfg->ota_origin), 0U)) ? 1U : 0U;
}

uint8_t PatrolConfig_CalibrationCompatible(void)
{
    return g_patrol_config.steering_counts_per_degree_milli == VEHICLE_STEERING_COUNTS_MILLI &&
        g_patrol_config.absolute_counts_per_degree_milli == VEHICLE_ABSOLUTE_COUNTS_MILLI &&
        g_patrol_config.steering_direction == VEHICLE_STEERING_DIRECTION &&
        g_patrol_config.absolute_direction == VEHICLE_ABSOLUTE_DIRECTION &&
        g_patrol_config.steering_tolerance_mdeg == VEHICLE_STEERING_TOLERANCE_MDEG;
}

uint8_t PatrolConfig_SingleTurnProfileReady(void)
{
    return (uint8_t)(g_patrol_config.calibrated && PatrolConfig_CalibrationCompatible() &&
                     g_patrol_config.absolute_center_count == VEHICLE_ABSOLUTE_CENTER_COUNT);
}

uint8_t PatrolConfig_SetCalibration(int32_t steering_center_count, uint32_t absolute_center_count,
                                 uint32_t steering_counts_per_degree_milli,
                                 uint32_t absolute_counts_per_degree_milli,
                                 int8_t steering_direction, int8_t absolute_direction,
                                 uint16_t tolerance_mdeg)
{
    PatrolRemoteConfigV5_t next = g_patrol_config;
    if (steering_counts_per_degree_milli != VEHICLE_STEERING_COUNTS_MILLI ||
        absolute_counts_per_degree_milli != VEHICLE_ABSOLUTE_COUNTS_MILLI ||
        steering_direction != VEHICLE_STEERING_DIRECTION || absolute_direction != VEHICLE_ABSOLUTE_DIRECTION ||
        tolerance_mdeg != VEHICLE_STEERING_TOLERANCE_MDEG ||
        tolerance_mdeg == 0U || tolerance_mdeg > 2000U ||
        (steering_direction != 1 && steering_direction != -1) ||
        (absolute_direction != 1 && absolute_direction != -1)) return 0U;
    next.steering_center_count = steering_center_count;
    next.absolute_center_count = absolute_center_count;
    next.steering_counts_per_degree_milli = steering_counts_per_degree_milli;
    next.absolute_counts_per_degree_milli = absolute_counts_per_degree_milli;
    next.steering_direction = steering_direction;
    next.absolute_direction = absolute_direction;
    next.steering_tolerance_mdeg = tolerance_mdeg;
    next.reserved_remote = 0U;
    next.calibrated = 1U;
    return PatrolConfig_Save(&next);
}

uint8_t PatrolConfig_ClearCalibration(void)
{
    PatrolRemoteConfigV5_t next = g_patrol_config;
    next.reserved_remote = 0U;
    next.calibrated = 0U;
    return PatrolConfig_Save(&next);
}
