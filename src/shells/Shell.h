#pragma once
// Which shell draws Home. The setting is read here and nowhere else: every screen that needs to know
// asks current().
#include <cstdint>
#include <memory>

#include "activities/ActivityManager.h"
#include "ShellKind.h"
#include "components/TenorMenuChrome.h"

class Activity;
class GfxRenderer;
class MappedInputManager;
struct SettingInfo;

namespace shell {

Kind current();
inline bool isUgly() { return current() == Kind::Ugly; }
// The parts every screen shares (header, key bar, status strip, list rows, notices, tips) are drawn by hand in
// tenor/ugly on the button readers. Each part asks this once; the X4 Pro keeps its touch parts.
inline bool uglyParts() { return isUgly() && !tenorchrome::kTouchShell; }

// tenor/ugly is a limited edition (ShellLimit.h): offered until its last day by the device clock. Past it,
// Settings no longer turns to it, and the next boot puts a device still on it back on tenor/cross.
bool uglyOffered();
// At boot, after the settings and the clock: a device left on tenor/ugly past the last day is put back on
// tenor/cross for good (its sleep screen too) and saved.
void expireIfOver();
// "tenor/xấu-như-chó is a limited edition[, here until d/m/yyyy]", for the Interface row and the question
// before turning to it. Valid until the next call.
const char* uglyLimitNote();

// The Home screen of the current shell. `item` says where a screen that closed wants to land.
std::unique_ptr<Activity> makeHome(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeMenuItem item,
                                   bool cleanInitialRefresh);

// The user chose a shell: the sleep screen that belongs to the shell the user just left follows to
// the one they entered (a screen chosen by hand stays), and Home is drawn again.
void changed();

// A value was changed on a settings screen: the shell may have a word to say about it.
void valueChanged(const SettingInfo& setting);

}  // namespace shell
