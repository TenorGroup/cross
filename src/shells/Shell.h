#pragma once
// Which shell draws Home. The setting is read here and nowhere else: every screen that needs to know
// asks current().
#include <cstdint>
#include <memory>

#include "activities/ActivityManager.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace shell {

enum class Kind : uint8_t { Cross = 0, Ugly = 1 };

Kind current();
inline bool isUgly() { return current() == Kind::Ugly; }

// The Home screen of the current shell. `item` says where a screen that closed wants to land.
std::unique_ptr<Activity> makeHome(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeMenuItem item,
                                   bool cleanInitialRefresh);

// The user chose a shell: the sleep screen that belongs to the shell the user just left follows to
// the one they entered (a screen chosen by hand stays), and Home is drawn again.
void changed();

}  // namespace shell
