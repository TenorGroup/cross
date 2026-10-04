#pragma once
// The entry points of the tenor/ugly shell. Everything else of it stays inside this directory.
#include <memory>

#include "activities/ActivityManager.h"
#include "activities/home/HomeRows.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace ugly {

// Tier 1, the diary: the first screen on waking and on leaving a book.
std::unique_ptr<Activity> makeDiary(GfxRenderer& renderer, MappedInputManager& mappedInput, bool cleanInitialRefresh);
// Tier 2, the desk. Only the X3 has one (the picture is its 528x792 frame); elsewhere deskAvailable() is false.
std::unique_ptr<Activity> makeDesk(GfxRenderer& renderer, MappedInputManager& mappedInput);
bool deskAvailable(const GfxRenderer& renderer);
// Tier 3, the notebook, opened on one of the five pages.
std::unique_ptr<Activity> makeNotebook(GfxRenderer& renderer, MappedInputManager& mappedInput, homerows::Page page);
// The box Settings raises before the interface turns to tenor/ugly: finishes with isCancelled false only when the user dared.
std::unique_ptr<Activity> makeSwitchConfirm(GfxRenderer& renderer, MappedInputManager& mappedInput);
// Where a screen that closed lands: the notebook page it belongs to, else the diary.
std::unique_ptr<Activity> makeHome(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeMenuItem item,
                                   bool cleanInitialRefresh);

}  // namespace ugly
