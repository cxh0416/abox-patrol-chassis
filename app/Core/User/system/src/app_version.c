#include "app_version.h"

const char g_vcu_fw_version_marker[] __attribute__((used, section(".fw_version"))) = VCU_APP_FW_VERSION_MARKER;

const char* Vcu_AppFwVersion(void) { return VCU_APP_FW_VERSION; }
