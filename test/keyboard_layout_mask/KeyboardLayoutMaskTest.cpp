// The saved keyboard-layout mask against the layouts this build ships (English only). Old builds
// also shipped Hebrew and Arabic keyboards (table positions 8 and 9, the persisted bits); the UI
// font has no glyphs for them. A mask naming them, or naming nothing this build has, must come out
// as the default keyboard, through the one function keyboard_layouts::enabled().
#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "activities/util/KeyboardLayoutSet.h"

namespace fui = freeink::ui;
namespace kl = keyboard_layouts;

static int failures = 0;
static void check(bool ok, const char* what, unsigned mask) {
  if (!ok) {
    std::printf("FAIL mask 0x%04x: %s\n", mask, what);
    ++failures;
  }
}

int main() {
  constexpr uint16_t HEBREW = 1u << 8;
  constexpr uint16_t ARABIC = 1u << 9;
  const uint16_t masks[] = {0,           HEBREW,          ARABIC,           HEBREW | ARABIC,
                            1,           1 | HEBREW,      1 | ARABIC,       1 | HEBREW | ARABIC,
                            0xFF00,      0xFFFF,          1u << 1,          (1u << 4) | HEBREW};
  const Language languages[] = {Language::EN, Language::VI, Language::ZH};
  for (const Language language : languages) {
    I18N.language = language;
    for (const uint16_t mask : masks) {
      SETTINGS.keyboardLayouts = mask;
      check(kl::COUNT == 1, "the table holds more than the English layout", mask);
      check(kl::enabled() == kl::bitAt(0), "a saved mask left something other than English enabled", mask);
      check(kl::startingLayout() == fui::KeyboardLayoutId::QwertyEn, "starts on a layout other than English", mask);
      check(kl::next(fui::KeyboardLayoutId::QwertyEn) == fui::KeyboardLayoutId::QwertyEn, "next leaves English", mask);
      check(kl::next(fui::KeyboardLayoutId::HebrewIl) == fui::KeyboardLayoutId::QwertyEn, "next from Hebrew", mask);
      check(kl::next(fui::KeyboardLayoutId::ArabicAr) == fui::KeyboardLayoutId::QwertyEn, "next from Arabic", mask);
    }
  }
  std::printf(failures ? "keyboard layout mask: %d failure(s)\n" : "keyboard layout mask: ok\n", failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
