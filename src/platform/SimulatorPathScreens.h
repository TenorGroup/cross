#pragma once

#ifdef SIMULATOR
#include <cstdlib>
#include <cstring>

#include "activities/settings/HomeButtonSettingsActivity.h"
#include "activities/settings/InfoUpdateActivity.h"
#include "activities/settings/KeyboardLayoutsActivity.h"
#if TENOR_CABLE_FLASH
#include "activities/settings/SdFirmwareUpdateActivity.h"
#endif

inline void simulatorPathScreen(GfxRenderer& renderer, MappedInputManager& input) {
  static bool opened = false;
  const char* target = std::getenv("CROSSPOINT_SIM_PATH_SCREEN");
  const char* at = std::getenv("CROSSPOINT_SIM_PATH_SCREEN_AT");
  const char* current = activityManager.currentName();
  // The cable update screens open over Home, as a computer would open them.
  const bool cableScreen = target && std::strncmp(target, "Cable", 5) == 0;
  if (opened || !target || !current || std::strcmp(current, cableScreen ? "Home" : "Settings") != 0 ||
      !activityManager.hasDrawnFrame() || (at && millis() < std::strtoul(at, nullptr, 10))) return;
  opened = true;
#if TENOR_CABLE_FLASH
  if (cableScreen) {
    cableflash::Begin begin;
    begin.size = 6330080;
    std::strcpy(begin.version, "v1.0.59");
    // CableConfirm: the question; CableUpdate: as after the recovery keys, straight to the session.
    activityManager.pushActivity(std::make_unique<SdFirmwareUpdateActivity>(
        renderer, input, begin, std::strcmp(target, "CableUpdate") == 0));
    return;
  }
#endif
  // Exercise the real screen on X3 when its menu row is gated by board capabilities.
  if (std::strcmp(target, "HomeButtonSettings") == 0)
    activityManager.pushActivity(std::make_unique<HomeButtonSettingsActivity>(renderer, input));
  else if (std::strcmp(target, "KeyboardLayouts") == 0)
    activityManager.pushActivity(std::make_unique<KeyboardLayoutsActivity>(renderer, input));
  else if (std::strcmp(target, "InfoUpdate") == 0)
    activityManager.pushActivity(std::make_unique<InfoUpdateActivity>(renderer, input));
}
#endif
