#include "patrol_mqtt_profile.h"

static const ABoxMqttCommandDescriptor commands[] = {
    {"power_on", "vehicle_chassis", ABOX_MQTT_COMMAND_DISCRETE_ACTION},
    {"power_off", "vehicle_chassis", ABOX_MQTT_COMMAND_SAFETY_STOP},
    {"chassis_control", "vehicle_chassis", ABOX_MQTT_COMMAND_CONTINUOUS_SESSION}
};

static const ABoxMqttReportDescriptor reports[] = {
    {"chassis_status", "vehicle_chassis", "state"}
};

static const ABoxMqttProfileDescriptor profile = {
    "vehicle_chassis", "1.0", commands, sizeof(commands) / sizeof(commands[0]),
    reports, sizeof(reports) / sizeof(reports[0])
};

static const ABoxMqttProfileDescriptor *const registered_profiles[] = {&profile};
static const ABoxMqttV4Registry registry = {registered_profiles, 1U};

// 返回本产品公共 MQTT V4 命令注册表。注册表位于静态只读存储，调用者不得修改或释放，内部维护动作不混入该公共表。
const ABoxMqttV4Registry *PatrolMqttProfile_Registry(void)
{
    return &registry;
}

// 返回本产品公共能力描述符。描述符用于公共能力清单与路由身份，不包含临时候选、现场进度或客户派生命名。
const ABoxMqttProfileDescriptor *PatrolMqttProfile_Get(void)
{
    return &profile;
}
