#include "ButtonSymbols.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
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
namespace {
constexpr int kNutNet = 14;

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

void veTamGiac(const GfxRenderer& r, const int x, const int y, const int net, const int dir, const bool doc,
               const bool black) {
  for (int c = 0; c < net; ++c) {
    const int dai = net - c;
    const int le = (dai - 1) / 2;
    const int half = net / 2;
    if (doc) {
      const int py = dir < 0 ? y + half - 1 - c : y - half + c;
      r.drawLine(x - le, py, x - le + dai - 1, py, black);
    } else {
      const int px = dir < 0 ? x + half - c : x - half + c;
      r.drawLine(px, y - le, px, y - le + dai - 1, black);
    }
  }
}

void veMuiTen(const GfxRenderer& r, const inlineSymbols::Shape shape, const int x, const int y, const bool black,
              const int net = kNutNet) {
  if (shape == inlineSymbols::Shape::Up || shape == inlineSymbols::Shape::Down) {
    veTamGiac(r, x, y, net, shape == inlineSymbols::Shape::Up ? -1 : 1, true, black);
    return;
  }
  const int dir = shape == inlineSymbols::Shape::Right ? 1 : -1;
  const int ngangY = y - 1;
  if (shape == inlineSymbols::Shape::Back) {
    const int le = net / 2 + 2;
    veTamGiac(r, x - le, ngangY, net, -1, false, black);
    veTamGiac(r, x + le, ngangY, net, -1, false, black);
    return;
  }
  veTamGiac(r, x, ngangY, net, dir, false, black);
}

void veDauChon(const GfxRenderer& r, const int x, const int y, const bool black, const int net) {
  const int size = net + 2;
  const int h = std::max(3, size / 2);
  const int selectY = y + (net >= 18 ? 1 : 0);
  inlineSymbols::drawShape(r, inlineSymbols::Shape::Select, x, selectY, size, black);
  if (net >= 18) r.drawPixel(x + h * 3 / 4, selectY - h, black);
}
}  // namespace

SymbolBounds horizontalBounds(const char* label, const int net) {
  const int id = labelId(label);
  if (id < 0 || id > 5) return {0, 0};
  const auto shape = resolve(id).shape;
  const int half = net / 2;
  const int triangleMin = half - net + 1;
  const int triangleMax = half;
  if (shape == Shape::Back) {
    const int separation = half + 2;
    return {separation - triangleMin, separation + triangleMax};
  }
  if (shape == Shape::Select) {
    const int h = std::max(3, (net + 2) / 2);
    return {h, h};
  }
  if (shape == Shape::Up || shape == Shape::Down) {
    const int rowLeft = (net - 1) / 2;
    return {rowLeft, net - 1 - rowLeft};
  }
  if (shape == Shape::Left) return {-triangleMin, triangleMax};
  if (shape == Shape::Right) return {half, net - 1 - half};
  return {0, 0};
}

bool drawLabel(const GfxRenderer& renderer, const char* label, int x, int y, const int net) {
  const int id = labelId(label);
  if (id < 0) return false;
  const auto spec = resolve(id);
  if (spec.label) return false;
  if (spec.shape == inlineSymbols::Shape::Select) {
    veDauChon(renderer, x, y, true, net);
  } else {
    veMuiTen(renderer, spec.shape, x, y, true, net);
  }
  return true;
}
}  // namespace buttonSymbols
