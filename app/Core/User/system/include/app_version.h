#ifndef PATROL_CHASSIS_APP_VERSION_H
#define PATROL_CHASSIS_APP_VERSION_H

#ifndef PATROL_APP_FW_VERSION
#define PATROL_APP_FW_VERSION "patrol-chassis-unversioned"
#endif
#define PATROL_APP_FW_VERSION_MARKER "PATROL_VEHICLE_CHASSIS_FW_VERSION=" PATROL_APP_FW_VERSION

extern const char g_patrol_fw_version_marker[];
// 返回本次构建嵌入的产品固件版本。返回静态只读字符串；源码或嵌入版本不能代替 dist 清单及固件哈希判断正式发布状态。
const char* Patrol_AppFwVersion(void);

#endif
