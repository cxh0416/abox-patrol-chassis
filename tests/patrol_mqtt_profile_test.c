#include <assert.h>
#include <string.h>
#include "patrol_mqtt_profile.h"
#include "abox_mqtt_profile_dump.h"

int main(int argc, char **argv)
{
    const ABoxMqttProfileDescriptor *profile = PatrolMqttProfile_Get();
    if (argc == 2 && strcmp(argv[1], "--describe") == 0) {
        ABoxMqttProfile_Dump(profile);
        return 0;
    }
    assert(ABoxMqttV4_RegistryValid(PatrolMqttProfile_Registry()));
    assert(ABoxMqttV4_FindProfile(PatrolMqttProfile_Registry(), "vehicle_chassis") == profile);
    assert(profile && strcmp(profile->name, "vehicle_chassis") == 0);
    assert(strcmp(profile->version, "1.0") == 0);
    assert(profile->command_count == 3U && profile->report_count == 1U);
    assert(ABoxMqttV4_FindCommand(profile, "power_on")->classification ==
           ABOX_MQTT_COMMAND_DISCRETE_ACTION);
    assert(ABoxMqttV4_FindCommand(profile, "power_off")->classification ==
           ABOX_MQTT_COMMAND_SAFETY_STOP);
    assert(ABoxMqttV4_FindCommand(profile, "chassis_control")->classification ==
           ABOX_MQTT_COMMAND_CONTINUOUS_SESSION);
    assert(strcmp(ABoxMqttV4_FindReport(profile, "chassis_status")->report_type, "state") == 0);
    return 0;
}
