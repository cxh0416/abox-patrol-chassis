#ifndef PATROL_CHASSIS_APP_VERSION_H
#define PATROL_CHASSIS_APP_VERSION_H

#ifndef PATROL_APP_FW_VERSION
#define PATROL_APP_FW_VERSION "patrol-chassis-unversioned"
#endif
#define PATROL_APP_FW_VERSION_MARKER "PATROL_VEHICLE_CHASSIS_FW_VERSION=" PATROL_APP_FW_VERSION

extern const char g_patrol_fw_version_marker[];
const char* Patrol_AppFwVersion(void);

#endif
