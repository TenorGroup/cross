#include "Shell.h"

#include "CrossPointSettings.h"
#include "activities/home/HomeActivity.h"
#include "ugly/UglyShell.h"

namespace shell {

Kind current() { return SETTINGS.uiShell == static_cast<uint8_t>(Kind::Ugly) ? Kind::Ugly : Kind::Cross; }

std::unique_ptr<Activity> makeHome(GfxRenderer& renderer, MappedInputManager& mappedInput, const HomeMenuItem item,
                                   const bool cleanInitialRefresh) {
  if (current() == Kind::Ugly) return ugly::makeHome(renderer, mappedInput, item, cleanInitialRefresh);
  return std::make_unique<HomeActivity>(renderer, mappedInput, item, cleanInitialRefresh);
}

void changed() {
  auto& settings = SETTINGS;
  // The two sleep screens tenor/cross ships with (the tenor picture, and the quotation the first-run setup
  // picks) give way to the doodle; leaving, the doodle gives way to the quotation. Any other choice stays.
  const bool shipped = settings.sleepScreen == CrossPointSettings::TENOR || settings.sleepScreen == CrossPointSettings::QUOTE;
  if (isUgly() && shipped) settings.sleepScreen = CrossPointSettings::UGLY;
  if (!isUgly() && settings.sleepScreen == CrossPointSettings::UGLY) settings.sleepScreen = CrossPointSettings::QUOTE;
  settings.saveToFile();
  activityManager.goHome(HomeMenuItem::RECENT_CONTINUE);
}

}  // namespace shell
