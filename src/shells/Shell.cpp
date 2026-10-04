#include "Shell.h"

#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "ShellLimit.h"
#include "activities/home/HomeActivity.h"
#include "ugly/UglyQuip.h"
#include "ugly/UglyShell.h"

namespace shell {

bool uglyOffered() {
  if constexpr (limit::UGLY_LAST_DAY == 0) return true;  // no last day yet: no clock read on every call
  return !limit::over(limit::UGLY_LAST_DAY, ReadingStatsStore::currentDay());
}

// The setting alone: the limit is applied to it at boot (expireIfOver), so a clock set mid-session flips nothing.

Kind current() { return kindOf(SETTINGS.uiShell); }

const char* uglyLimitNote() {
  static char note[120];
  const uint32_t d = limit::UGLY_LAST_DAY;
  if (d == 0) {
    snprintf(note, sizeof(note), tr(STR_SHELL_UGLY_LIMITED), tr(STR_SHELL_UGLY));
  } else {
    char day[12];
    snprintf(day, sizeof(day), "%d/%d/%d", static_cast<int>(d % 100), static_cast<int>(d / 100 % 100), static_cast<int>(d / 10000));
    snprintf(note, sizeof(note), tr(STR_SHELL_UGLY_LIMITED_UNTIL), tr(STR_SHELL_UGLY), day);
  }
  return note;
}

std::unique_ptr<Activity> makeHome(GfxRenderer& renderer, MappedInputManager& mappedInput, const HomeMenuItem item,
                                   const bool cleanInitialRefresh) {
  if (current() == Kind::Ugly) return ugly::makeHome(renderer, mappedInput, item, cleanInitialRefresh);
  return std::make_unique<HomeActivity>(renderer, mappedInput, item, cleanInitialRefresh);
}

void valueChanged(const SettingInfo& setting) {
  if (isUgly()) ugly::noteValue(setting);
}

namespace {
// The sleep screen follows the shell and the settings are saved.
void settle() {
  auto& settings = SETTINGS;
  using S = CrossPointSettings;
  // The two sleep screens tenor/cross ships with (the tenor picture, and the quotation the first-run setup
  // picks) give way to the doodle, and the one given way is remembered; leaving, it comes back. A doodle
  // with nothing remembered gives way to the quotation. Any other choice stays.
  if (isUgly()) {
    if (settings.sleepScreen == S::TENOR || settings.sleepScreen == S::QUOTE) {
      settings.uiShellSleepMemo = static_cast<uint8_t>(settings.sleepScreen + 1);
      settings.sleepScreen = S::UGLY;
    }
  } else {
    if (settings.sleepScreen == S::UGLY) {
      const uint8_t memo = settings.uiShellSleepMemo ? static_cast<uint8_t>(settings.uiShellSleepMemo - 1) : S::QUOTE;
      settings.sleepScreen = memo == S::TENOR || memo == S::QUOTE ? memo : S::QUOTE;
    }
    settings.uiShellSleepMemo = 0;
  }
  settings.saveToFile();
}
}  // namespace

void changed() {
  settle();
  activityManager.goHome(HomeMenuItem::RECENT_CONTINUE);
}

void expireIfOver() {
  if (SETTINGS.uiShell != static_cast<uint8_t>(Kind::Ugly) || uglyOffered()) return;
  SETTINGS.uiShell = static_cast<uint8_t>(Kind::Cross);
  settle();
}

}  // namespace shell
