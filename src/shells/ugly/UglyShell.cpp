#include "UglyShell.h"

#include "UglyDesk.h"
#include "UglyDiary.h"
#include "UglyNotebook.h"
#include "UglySwitch.h"

namespace ugly {

std::unique_ptr<Activity> makeDiary(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool cleanInitialRefresh) {
  return std::make_unique<Diary>(renderer, mappedInput, cleanInitialRefresh);
}

std::unique_ptr<Activity> makeDesk(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<Desk>(renderer, mappedInput);
}

std::unique_ptr<Activity> makeNotebook(GfxRenderer& renderer, MappedInputManager& mappedInput, const homerows::Page page) {
  return std::make_unique<Notebook>(renderer, mappedInput, page);
}

std::unique_ptr<Activity> makeSwitchConfirm(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<SwitchConfirm>(renderer, mappedInput);
}

std::unique_ptr<Activity> makeHome(GfxRenderer& renderer, MappedInputManager& mappedInput, const HomeMenuItem item,
                                   const bool cleanInitialRefresh) {
  using homerows::Page;
  switch (item) {
    case HomeMenuItem::FILE_BROWSER:
    case HomeMenuItem::OPDS_BROWSER:
    case HomeMenuItem::LIBRARY:
      return makeNotebook(renderer, mappedInput, Page::Folder);
    case HomeMenuItem::RECENTS:
      return makeNotebook(renderer, mappedInput, Page::Recent);
    case HomeMenuItem::STATS_TAB:
      return makeNotebook(renderer, mappedInput, Page::Stats);
    case HomeMenuItem::FAVORITES_TAB:
      return makeNotebook(renderer, mappedInput, Page::Favorites);
    case HomeMenuItem::FILE_TRANSFER:
    case HomeMenuItem::SETTINGS_MENU:
      return makeNotebook(renderer, mappedInput, Page::Settings);
    case HomeMenuItem::NONE:
    case HomeMenuItem::RECENT_CONTINUE:
      break;
  }
  return makeDiary(renderer, mappedInput, cleanInitialRefresh);
}

}  // namespace ugly
