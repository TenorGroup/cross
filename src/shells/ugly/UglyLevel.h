#pragma once
// How ugly tenor/ugly is, and where the setting for it shows. No hardware dependencies: the settings list and its host
// tests include it as they are.
#include <cstdint>

#include "shells/ShellKind.h"

namespace ugly::logic {

// Plain draws the baked straight letters as they are. Af ("ugly af", the default) turns, shrinks and lifts every
// letter as it is drawn, and lets the line jump as well.
enum class Level : uint8_t { Plain = 0, Af = 1 };
// The saved value of the setting (0 plain, 1 af). A file without the key, or with a value nobody knows, is af.
inline Level levelOf(const uint8_t saved) { return saved == 0 ? Level::Plain : Level::Af; }
// The row "Ugliness" is there only while the selected shell is tenor/ugly.
inline bool levelRowShown(const uint8_t savedShell) { return shell::kindOf(savedShell) == shell::Kind::Ugly; }

}  // namespace ugly::logic
