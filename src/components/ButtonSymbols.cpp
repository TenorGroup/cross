#include "ButtonSymbols.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "fontIds.h"
extern HalGPIO gpio;
namespace buttonSymbols {
using inlineSymbols::Shape;
bool font(int id) { return id == UI_10_FONT_ID || id == UI_12_FONT_ID || id == SMALL_FONT_ID; }
inlineSymbols::Spec resolve(int id) {
  if (id == 10)
    return {SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && !gpio.hasTouch() ? Shape::MarginStar : Shape::Star,
            nullptr};
  if (id == 9) return {Shape::Erase, nullptr};
  if (id == 6) return {Shape::Star, nullptr};
  constexpr Shape shapes[] = {Shape::Select, Shape::Back, Shape::Up, Shape::Down, Shape::Left, Shape::Right};
  // The last two tokens follow the configured reading-side button layout.
  if (id == 7 || id == 8) {
    const bool reverse = SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
    id = (id == 7) != reverse ? 5 : 4;
  }
  if (id < 0 || id > 5) return {Shape::None, ""};
  if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && SETTINGS.tenorButtonSymbols && !gpio.hasTouch())
    return {shapes[id], nullptr};
  const StrId labels[] = {StrId::STR_SELECT,   StrId::STR_BACK,     StrId::STR_DIR_UP,
                          StrId::STR_DIR_DOWN, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT};
  return {shapes[id], I18N.get(labels[id])};
}
void install() { inlineSymbols::configure(resolve, font); }
bool drawLabel(const GfxRenderer& renderer, const char* label, int x, int y) {
  const StrId labels[] = {StrId::STR_SELECT,   StrId::STR_BACK,     StrId::STR_DIR_UP,
                          StrId::STR_DIR_DOWN, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT};
  int id = -1;
  const auto* bytes = reinterpret_cast<const unsigned char*>(label);
  if (strlen(label) == 3 && bytes[0] == 0xEE && bytes[1] == 0x84 && bytes[2] >= 0x80 && bytes[2] <= 0x85)
    id = bytes[2] - 0x80;
  for (int i = 0; i < 6; ++i)
    if (strcmp(label, I18N.get(labels[i])) == 0) id = i;
  if (strcmp(label, tr(STR_TOGGLE)) == 0 || strcmp(label, tr(STR_OPEN)) == 0 || strcmp(label, tr(STR_CONFIRM)) == 0)
    id = 0;
  if (id < 0) return false;
  const auto spec = resolve(id);
  if (spec.label) return false;
  inlineSymbols::drawShape(renderer, spec.shape, x, y, id == 0 ? 26 : 20, true);
  return true;
}
}  // namespace buttonSymbols
