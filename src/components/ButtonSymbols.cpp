#include "ButtonSymbols.h"

#include <I18n.h>
#include <InlineSymbolBitmaps.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "TenorMenuChrome.h"
#include "fontIds.h"
namespace buttonSymbols {
using inlineSymbols::Shape;
bool font(int id) { return id == UI_10_FONT_ID || id == UI_12_FONT_ID || id == SMALL_FONT_ID; }
inlineSymbols::Spec resolve(int id) {
  if (id == 10)
    return {tenorchrome::enabled() ? Shape::MarginPin : Shape::Star, nullptr,
            !tenorchrome::kTouchShell && !SETTINGS.tenorSideArrows ? 15 : 8};
  if (id == 9) return {Shape::Erase, nullptr};
  if (id == 6) return {Shape::Star, nullptr};
  constexpr Shape shapes[] = {Shape::Select, Shape::Back, Shape::Up, Shape::Down, Shape::Left, Shape::Right};
  // The last two tokens follow the configured reading-side button layout.
  if (id == 7 || id == 8) {
    const bool reverse = SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
    id = (id == 7) != reverse ? 5 : 4;
  }
  if (id < 0 || id > 5) return {Shape::None, ""};
  if (tenorchrome::enabled() && SETTINGS.tenorButtonSymbols)
    return {shapes[id], nullptr};
  const StrId labels[] = {StrId::STR_SELECT,   StrId::STR_BACK,     StrId::STR_DIR_UP,
                          StrId::STR_DIR_DOWN, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT};
  return {shapes[id], I18N.get(labels[id])};
}
void install() { inlineSymbols::configure(resolve, font); }
int labelId(const char* label) {
  if (!label) return -1;
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
  return id;
}

SymbolBounds horizontalBounds(const char* label, const int net) {
  const int id = labelId(label);
  if (id < 0 || id > 5) return {0, 0};
  const auto shape = resolve(id).shape;
  if (shape == Shape::Back || shape == Shape::Select || shape == Shape::Up || shape == Shape::Down ||
      shape == Shape::Left || shape == Shape::Right) {
    const int w = inlineSymbols::glyphWidth(shape, net);
    return {w / 2, w - 1 - w / 2};
  }
  return {0, 0};
}

// Rows to move a key symbol from the centre of its own box to where the strip puts it. The four keys share one shaft
// (Back's), and the whole row stands from Enter's top row to Back's bottom row. Enter's top row is the first row of the
// strip, which puts Back's top row 6 (net 14) or 8 (net 18) rows above the anchor y; every glyph then hangs from there
// by its own `top`, counted from Back's top row.
static int barShift(const Shape shape, const int net) {
  using namespace inlineSymbolBitmaps;
  const bool large = net >= 16;
  const Glyph* glyph = nullptr;
  if (shape == Shape::Back) glyph = large ? &kBackNet18 : &kBackNet14;
  if (shape == Shape::Select) glyph = large ? &kSelectNet18 : &kSelectNet14;
  if (shape == Shape::Up) glyph = large ? &kUpNet18 : &kUpNet14;
  if (shape == Shape::Down) glyph = large ? &kDownNet18 : &kDownNet14;
  if (shape == Shape::Left) glyph = large ? &kLeftNet18 : &kLeftNet14;
  if (shape == Shape::Right) glyph = large ? &kRightNet18 : &kRightNet14;
  return glyph ? glyph->top + glyph->height / 2 - (large ? 8 : 6) : 0;
}

bool drawLabel(const GfxRenderer& renderer, const char* label, int x, int y, const int net) {
  const int id = labelId(label);
  if (id < 0) return false;
  const auto spec = resolve(id);
  if (spec.label) return false;
  inlineSymbols::drawShape(renderer, spec.shape, x, y + barShift(spec.shape, net), net, true);
  return true;
}
}  // namespace buttonSymbols
