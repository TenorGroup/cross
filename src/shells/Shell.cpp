#include "Shell.h"

#include "CrossPointSettings.h"
#include "activities/home/HomeActivity.h"
#include "ugly/UglyQuip.h"
#include "ugly/UglyShell.h"

namespace shell {

Kind current() { return kindOf(SETTINGS.uiShell); }

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

}  // namespace shell
