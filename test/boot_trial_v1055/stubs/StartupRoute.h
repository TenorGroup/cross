#pragma once

enum class HomeMenuItem { NONE, RECENT_CONTINUE, SETTINGS_MENU, RECENTS, DESK };
struct CrossPointSettings {
  enum UGLY_START_SCREEN {
    UGLY_START_BOOK = 0,
    UGLY_START_DIARY = 1,
    UGLY_START_RECENT = 2,
    UGLY_START_DESK = 3
  };
};
namespace shell {
inline bool isUgly() { return false; }
}
constexpr auto uglyStart = CrossPointSettings::UGLY_START_DIARY;
