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

const ABoxMqttV4Registry *PatrolMqttProfile_Registry(void)
{
    return &registry;
}

const ABoxMqttProfileDescriptor *PatrolMqttProfile_Get(void)
{
    return &profile;
}
