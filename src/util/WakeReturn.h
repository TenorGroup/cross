#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "util/WakePlace.h"

// The Settings tab and row a silent restart comes back to (src/main.cpp), kept in RTC memory beside the restart's
// target. Only a Settings place is kept: a Wi-Fi task opened from Settings restarts on the heap of a fresh boot and
// must hand the user back to the tab it left. The slot is RTC_NOINIT (garbage after a power loss, no constructor): it
// is read only when the restart's magic is valid, and every restart writes it first.
namespace wakereturn {

constexpr int KEY_MAX = 32;  // a settings JSON key or "action/N"

struct Slot {
  uint32_t tabPlusOne;  // 0: no place
  char key[KEY_MAX];
};

inline void store(Slot& slot, const WakePlace& place) {
  slot.tabPlusOne = 0;
  slot.key[0] = '\0';
  if (place.kind != WakeKind::SettingsTab || place.tab < 0 || place.tab >= wakeplace::SETTINGS_TABS) return;
  std::strncpy(slot.key, place.name.c_str(), KEY_MAX - 1);
  slot.key[KEY_MAX - 1] = '\0';
  slot.tabPlusOne = static_cast<uint32_t>(place.tab) + 1;
}

inline WakePlace load(const Slot& slot) {
  WakePlace place;
  if (slot.tabPlusOne == 0 || slot.tabPlusOne > static_cast<uint32_t>(wakeplace::SETTINGS_TABS)) return place;
  place.kind = WakeKind::SettingsTab;
  place.tab = static_cast<int16_t>(slot.tabPlusOne - 1);
  place.name.assign(slot.key, strnlen(slot.key, KEY_MAX - 1));
  return place;
}

}  // namespace wakereturn
