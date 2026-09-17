// Host definition of the one CrossPointSettings member FontInstaller calls.
//
// src/FontInstaller.cpp includes "CrossPointSettings.h" as a *sibling* quoted
// include, so the real src/CrossPointSettings.h - and its real field layout - is
// what this target compiles no matter what is on the include path. Only
// clearSdFontFamily() has to be defined here: the production body lives in
// src/CrossPointSettings.cpp, whose I18n/SettingsList/ReaderMenuLayout/save
// dependencies are far outside this slice.
//
// This host body mirrors the part of the production contract FontInstaller
// depends on - the active SD family name is dropped - and deliberately does not
// model the settings-layer side effects (built-in point-size snap, settings
// write). The tests assert only the family-name outcome.

#include <CrossPointSettings.h>

void CrossPointSettings::clearSdFontFamily() { sdFontFamilyName[0] = '\0'; }
