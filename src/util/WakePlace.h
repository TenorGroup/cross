#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/WakeBook.h"

// Where the screen in front stood when the machine went to sleep, kept in state.json for the wake
// that follows ("On wake" = pick up where I left off). None is Home, as before.
enum class WakeKind : uint8_t { None, HomeTab, Book, Files, SettingsTab };

struct WakePlace {
  WakeKind kind = WakeKind::None;
  uint8_t shell = 0;  // uiShell at sleep: a place of one shell means nothing to the other
  int16_t tab = 0;  // HomeTab, SettingsTab
  int16_t row = 0;  // HomeTab: the line the cursor was on, when it has no name
  std::string path;  // Book: the book; Files: the folder
  std::string name;  // Files: the entry; HomeTab: the entry (book, folder entry, pin, group); SettingsTab: the key of the row
};

namespace wakeplace {

constexpr int HOME_TABS = 5;
constexpr int SETTINGS_TABS = 9;

// The place a wake reopens, or kind None for Home. Only a wake out of deep sleep (the chip's reset reason,
// not a power-on, a watchdog or a brownout) reopens anything, and only in the shell it was left in. A book follows the
// "wake into the book" rules (still in Recent, still on the card); a folder that is gone gives way to the nearest
// one above it.
template <typename Book, typename OnCard, typename IsFolder>
WakePlace resolve(const bool deepSleepReset, const uint8_t shell, WakePlace place, const std::vector<Book>& recents,
                  OnCard&& onCard, IsFolder&& isFolder) {
  if (!deepSleepReset || place.shell != shell) return {};
  switch (place.kind) {
    case WakeKind::Book:
      return wakebook::bookToOpen(true, true, place.path, recents, onCard).empty() ? WakePlace{} : place;
    case WakeKind::Files:
      if (place.path.empty()) return {};
      while (place.path != "/" && !isFolder(place.path)) {
        const auto cut = place.path.find_last_of('/');
        place.path = cut == 0 || cut == std::string::npos ? "/" : place.path.substr(0, cut);
        place.name.clear();
      }
      return place;
    case WakeKind::HomeTab:
      return place.tab >= 0 && place.tab < HOME_TABS && place.row >= 0 ? place : WakePlace{};
    case WakeKind::SettingsTab:
      return place.tab >= 0 && place.tab < SETTINGS_TABS && place.row >= 0 ? place : WakePlace{};
    default:
      return {};
  }
}

}  // namespace wakeplace
