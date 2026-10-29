#ifndef PATROL_MQTT_CONFIG_ADAPTER_H
#define PATROL_MQTT_CONFIG_ADAPTER_H

#include <stdint.h>
#include "abox_mqtt_trial.h"
#include "ota_info.h"

// 把产品 Config V5 转换为公共 MQTT 运行配置。校验 Broker、认证和 TLS 字段并关联当前凭据，输出结构仍可能引用原配置存储，调用方须保证生命周期。
uint8_t PatrolMqttConfig_FromV5(const PatrolRemoteConfigV5_t *config,
                                ABoxMqttConfig *runtime);

#endif
