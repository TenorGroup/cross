#include "UglySleep.h"

#include "UglySleepSet.h"

namespace ugly {

bool drawSleep(GfxRenderer& renderer) { return sleepset::drawScreen(renderer); }

}  // namespace ugly
