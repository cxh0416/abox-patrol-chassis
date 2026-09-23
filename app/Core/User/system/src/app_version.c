#include "app_version.h"

const char g_patrol_fw_version_marker[] __attribute__((used, section(".fw_version"))) = PATROL_APP_FW_VERSION_MARKER;

const char* Patrol_AppFwVersion(void) { return PATROL_APP_FW_VERSION; }
