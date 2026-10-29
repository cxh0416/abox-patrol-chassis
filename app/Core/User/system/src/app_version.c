#include "app_version.h"

const char g_patrol_fw_version_marker[] __attribute__((used, section(".fw_version"))) = PATROL_APP_FW_VERSION_MARKER;

// 返回本次构建嵌入的产品固件版本。返回静态只读字符串；源码或嵌入版本不能代替 dist 清单及固件哈希判断正式发布状态。
const char* Patrol_AppFwVersion(void) { return PATROL_APP_FW_VERSION; }
