#include "mqtt_config_adapter.h"
#include "patrol_config.h"

#include <string.h>

// 检查 MQTT 配置文本是否有合法容量内终止符和允许字符。拒绝空指针、越界文本及不支持字符，供 V5 到公共配置转换使用。
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

// 把产品 Config V5 转换为公共 MQTT 运行配置。校验 Broker、认证和 TLS 字段并关联当前凭据，输出结构仍可能引用原配置存储，调用方须保证生命周期。
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
