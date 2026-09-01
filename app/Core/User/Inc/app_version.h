#ifndef PATROL_CHASSIS_APP_VERSION_H
#define PATROL_CHASSIS_APP_VERSION_H

#ifndef VCU_APP_FW_VERSION
#define VCU_APP_FW_VERSION "patrol-chassis-0.1.1"
#endif
#define VCU_APP_FW_VERSION_MARKER "PATROL_VEHICLE_CHASSIS_FW_VERSION=" VCU_APP_FW_VERSION

extern const char g_vcu_fw_version_marker[];
const char* Vcu_AppFwVersion(void);

#endif
