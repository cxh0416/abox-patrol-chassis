#include "mqtt_config_adapter.h"
#include "patrol_config.h"

#include <string.h>

static uint8_t text_valid(const char *value, uint16_t capacity)
{
    uint16_t i;
    if (value == 0 || capacity == 0U || value[0] == '\0' || (uint8_t)value[0] == 0xFFU) return 0U;
    for (i = 0U; i < capacity; ++i) {
        uint8_t ch = (uint8_t)value[i];
        if (ch == '\0') return 1U;
        if (ch < 0x21U || ch > 0x7EU || ch == '"' || ch == '\\') return 0U;
    }
    return 0U;
}

uint8_t PatrolMqttConfig_FromV5(const PatrolRemoteConfigV5_t *config,
                                ABoxMqttConfig *runtime)
{
    if (config == 0 || runtime == 0 || config->version != PATROL_CONFIG_VERSION ||
        config->length != sizeof(*config) || config->mqtt_tls_enabled > 1U ||
        !text_valid(config->mqtt_host, sizeof(config->mqtt_host)) || config->mqtt_port == 0U ||
        !text_valid(config->mqtt_username, sizeof(config->mqtt_username)) ||
        !text_valid(config->mqtt_password, sizeof(config->mqtt_password))) return 0U;
    runtime->host = config->mqtt_host;
    runtime->port = config->mqtt_port;
    runtime->username = config->mqtt_username;
    runtime->password = config->mqtt_password;
    runtime->tls_enabled = config->mqtt_tls_enabled;
    runtime->tls_profile_id = config->mqtt_tls_profile_id;
    runtime->tls_credentials = *PatrolConfig_MqttCredentials();
    return 1U;
}
