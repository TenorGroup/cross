#pragma once

enum class HomeMenuItem { NONE, RECENT_CONTINUE, SETTINGS_MENU, RECENTS, DESK };
struct CrossPointSettings {
  enum WAKE_TARGET { WAKE_HOME = 0, WAKE_BOOK = 1, WAKE_RESUME = 2, WAKE_RECENT = 3 };
};
namespace shell {
inline bool isUgly() { return false; }
}
constexpr auto wakeTarget = CrossPointSettings::WAKE_HOME;
#include "util/WakePlace.h"
// No place is kept by the wake in this suite: a wake goes to a book or to Home.
constexpr struct { WakeKind kind = WakeKind::None; } wakePlace;
constexpr bool wakeToScreen = false;
struct HalDisplay { static constexpr int FULL_REFRESH = 1; };
