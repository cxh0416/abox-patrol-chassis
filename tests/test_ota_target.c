#include <assert.h>
#include "patrol_ota_target.h"
int main(void)
{
    assert(PatrolOtaTarget_VersionMatches("patrol-chassis-0.2.112-gd", 1));
    assert(!PatrolOtaTarget_VersionMatches("patrol-chassis-0.2.112", 1));
    assert(!PatrolOtaTarget_VersionMatches("patrol-chassis-0.2.112-gd", 0));
    assert(PatrolOtaTarget_VersionMatches("patrol-chassis-0.2.112", 0));
    assert(!PatrolOtaTarget_VersionMatches("", 0));
    assert(!PatrolOtaTarget_VersionMatches(0, 1));
    return 0;
}
