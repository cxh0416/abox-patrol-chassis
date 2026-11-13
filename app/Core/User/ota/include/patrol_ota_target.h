#ifndef PATROL_OTA_TARGET_H
#define PATROL_OTA_TARGET_H
#include <stddef.h>
#include <string.h>

/* The service verifies the image marker too; this local check rejects the
 * other board's normal version before an OTA transaction is started. */
static inline int PatrolOtaTarget_VersionMatches(const char *version, int gd32)
{
    size_t length;
    int gd_suffix;
    if (!version || !(length = strlen(version))) return 0;
    gd_suffix = length >= 3U && strcmp(version + length - 3U, "-gd") == 0;
    return gd32 ? gd_suffix : !gd_suffix;
}
#endif
