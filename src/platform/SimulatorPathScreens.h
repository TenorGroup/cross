#pragma once

#ifdef SIMULATOR
#include <cstdlib>
#include <cstring>

#include "activities/settings/HomeButtonSettingsActivity.h"
#include "activities/settings/InfoUpdateActivity.h"
#include "activities/settings/KeyboardLayoutsActivity.h"

inline void simulatorPathScreen(GfxRenderer& renderer, MappedInputManager& input) {
  static bool opened = false;
  const char* target = std::getenv("CROSSPOINT_SIM_PATH_SCREEN");
  const char* at = std::getenv("CROSSPOINT_SIM_PATH_SCREEN_AT");
  const char* current = activityManager.currentName();
  if (opened || !target || !current || std::strcmp(current, "Settings") != 0 ||
      !activityManager.hasDrawnFrame() || (at && millis() < std::strtoul(at, nullptr, 10))) return;
  opened = true;
  // Exercise the real screen on X3 when its menu row is gated by board capabilities.
  if (std::strcmp(target, "HomeButtonSettings") == 0)
    activityManager.pushActivity(std::make_unique<HomeButtonSettingsActivity>(renderer, input));
  else if (std::strcmp(target, "KeyboardLayouts") == 0)
    activityManager.pushActivity(std::make_unique<KeyboardLayoutsActivity>(renderer, input));
  else if (std::strcmp(target, "InfoUpdate") == 0)
    activityManager.pushActivity(std::make_unique<InfoUpdateActivity>(renderer, input));
}
#endif
