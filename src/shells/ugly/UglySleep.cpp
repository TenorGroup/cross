#include "UglySleep.h"

#include <I18n.h>

#include "UglySleepSet.h"

namespace ugly {

bool drawSleep(GfxRenderer& renderer) { return sleepset::drawScreen(renderer); }

const char* sleepNotice() { return tr(STR_UGLY_YAWNING); }

}  // namespace ugly
