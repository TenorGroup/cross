#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/WakeBook.h"

// Where the screen in front stood when the machine went to sleep, kept in state.json for the wake
// that follows ("On wake" = pick up where I left off). None is Home, as before.
// Connect (Connections: the list, a session, the font store) is the last: a build from before it reads it as the
// kind it does not know, and goes Home.
enum class WakeKind : uint8_t { None, HomeTab, Book, Files, SettingsTab, Connect };

struct WakePlace {
  WakeKind kind = WakeKind::None;
  uint8_t shell = 0;  // uiShell at sleep: a place of one shell means nothing to the other
  int16_t tab = 0;  // HomeTab, SettingsTab
  int16_t row = 0;  // HomeTab: the line the cursor was on, when it has no name; Connect: the NetworkMode of the session
  std::string path;  // Book: the book; Files: the folder; Connect: the book the session was opened from, if any
  std::string name;  // Files: the entry; HomeTab: the entry (book, folder entry, pin, group); SettingsTab: the key of the row
};

namespace wakeplace {

constexpr int HOME_TABS = 5;
constexpr int SETTINGS_TABS = 9;

// The place a wake reopens, or kind None for Home, only in the shell it was left in. A book follows the "wake into the
// book" rules: a power-button start opens it too (a sleep that lost its power comes back as a power-on), when it is
// still in Recent and on the card. Any other place needs a wake out of deep sleep (the chip's reset reason, not a
// power-on, a watchdog or a brownout). A folder that is gone gives way to the nearest
// one above it. A Connections place needs a mode this board has a row for (the X3 has no USB drive and no WebDAV);
// the book it came from, if that is gone, is dropped and the place kept.
template <typename Book, typename OnCard, typename IsFolder, typename ModeOnBoard>
WakePlace resolve(const bool deepSleepReset, const bool powerButtonStart, const uint8_t shell, WakePlace place,
                  const std::vector<Book>& recents, OnCard&& onCard, IsFolder&& isFolder, ModeOnBoard&& modeOnBoard) {
  if (place.shell != shell) return {};
  if (place.kind == WakeKind::Book)
    return wakebook::bookToOpen(deepSleepReset || powerButtonStart, true, place.path, recents, onCard).empty()
               ? WakePlace{}
               : place;
  if (!deepSleepReset) return {};
  switch (place.kind) {
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
    case WakeKind::Connect:
      if (!modeOnBoard(place.row)) return {};
      if (wakebook::bookToOpen(true, true, place.path, recents, onCard).empty()) place.path.clear();
      return place;
    default:
      return {};
  }
}

}  // namespace wakeplace
