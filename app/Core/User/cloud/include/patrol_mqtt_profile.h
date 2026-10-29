#ifndef PATROL_MQTT_PROFILE_H
#define PATROL_MQTT_PROFILE_H

#include "abox_mqtt_v4.h"

// 返回本产品公共能力描述符。描述符用于公共能力清单与路由身份，不包含临时候选、现场进度或客户派生命名。
const ABoxMqttProfileDescriptor *PatrolMqttProfile_Get(void);
// 返回本产品公共 MQTT V4 命令注册表。注册表位于静态只读存储，调用者不得修改或释放，内部维护动作不混入该公共表。
const ABoxMqttV4Registry *PatrolMqttProfile_Registry(void);

#endif
