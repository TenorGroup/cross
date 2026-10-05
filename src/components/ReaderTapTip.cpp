#include "ReaderTapTip.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "TenorMenuChrome.h"
#include "fontIds.h"

namespace {
bool shown = false;
readertap::Rules shownRules{};

constexpr int LINE = 3;

// A label in a white pill over the page, centred on (cx, cy); split on its middle space when wider than
// maxWidth. On a gray plane only its box is cleared, so the page's grays stay out of it.
void label(const GfxRenderer& r, const bool plane, const char* text, int cx, const int cy, const int maxWidth) {
  constexpr int font = UI_12_FONT_ID, padX = 14, padY = 8;
  std::string a = text, b;
  if (r.getTextWidth(font, text) > maxWidth - 2 * padX) {
    const auto space = a.find(' ', a.size() / 3);
    if (space != std::string::npos) {
      b = a.substr(space + 1);
      a.resize(space);
    }
  }
  const int lh = r.getLineHeight(font);
  const int tw = std::max(r.getTextWidth(font, a.c_str()), b.empty() ? 0 : r.getTextWidth(font, b.c_str()));
  const int w = tw + 2 * padX, h = lh * (b.empty() ? 1 : 2) + 2 * padY;
  // A label wider than its zone (a narrow back column) stays on the screen.
  const int x = std::max(4, std::min(cx - w / 2, r.getScreenWidth() - 4 - w)), y = cy - h / 2;
  cx = x + w / 2;
  r.fillRect(x, y, w, h, plane);
  if (plane) return;
  tenorchrome::drawPillRing(r, x, y, w, h, 2, false);
  r.drawText(font, cx - r.getTextWidth(font, a.c_str()) / 2, y + padY, a.c_str());
  if (!b.empty()) r.drawText(font, cx - r.getTextWidth(font, b.c_str()) / 2, y + padY + lh, b.c_str());
}

// A boundary between zones: black on the page, no gray under it.
void edge(const GfxRenderer& r, const int x, const int y, const int w, const int h) { r.fillRect(x, y, w, h, true); }
}  // namespace

void readertip::open(const readertap::Rules& rules) {
  shownRules = rules;
  shown = true;
}
void readertip::close() { shown = false; }
bool readertip::isOpen() { return shown; }
const readertap::Rules& readertip::rules() { return shownRules; }

void readertip::draw(const GfxRenderer& r) {
  if (!shown) return;
  using readertap::Zone;
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  const bool plane = r.getRenderMode() != GfxRenderer::BW && !r.grayPlanesAreAbsolute();
  const auto& rules = shownRules;
  const auto top = readertap::zoneBox(Zone::TopMenu, w, h, rules);
  const auto foot = readertap::zoneBox(Zone::TextMenu, w, h, rules);
  const auto back = readertap::zoneBox(Zone::Prev, w, h, rules);
  const auto next = readertap::zoneBox(Zone::Next, w, h, rules);
  const auto cell = readertap::zoneBox(Zone::Menu, w, h, rules);
  if (top.h) edge(r, 0, top.h - LINE / 2, w, LINE);
  if (foot.h) edge(r, 0, foot.y - LINE / 2, w, LINE);
  if (back.w && next.w) edge(r, rules.inverted ? back.x - LINE / 2 : back.w - LINE / 2, back.y, LINE, back.h);
  if (cell.w) {
    edge(r, cell.x, cell.y, cell.w, LINE);
    edge(r, cell.x, cell.y + cell.h - LINE, cell.w, LINE);
    edge(r, cell.x, cell.y, LINE, cell.h);
    edge(r, cell.x + cell.w - LINE, cell.y, LINE, cell.h);
  }
  if (top.h) label(r, plane, tr(STR_TIP_TOP_MENU), w / 2, top.h / 2, w);
  if (foot.h) label(r, plane, tr(STR_TIP_TEXT_MENU), w / 2, foot.y + foot.h / 2, w);
  if (cell.w) label(r, plane, tr(STR_TIP_READER_MENU), cell.x + cell.w / 2, cell.y + cell.h / 2, cell.w);
  if (back.w) label(r, plane, tr(STR_PREV_PAGE_GESTURE), back.x + back.w / 2, back.y + back.h / 2, back.w);
  // The forward label sits between the top band and the centre cell, the button between the cell and the foot.
  const int nextCy = cell.w ? (next.y + cell.y) / 2 : next.y + next.h / 3;
  if (next.w) label(r, plane, tr(STR_NEXT_PAGE_GESTURE), next.x + next.w / 2, nextCy, next.w);
  if (!next.w) return;
  const auto button = readertap::tipButton(w, h, rules);
  r.fillRect(button.x, button.y, button.w, button.h, plane);
  if (plane) return;
  tenorchrome::drawPillRing(r, button.x, button.y, button.w, button.h, 3, false);
  // The button's words in the label font, or the small one when a wide back column narrows it.
  const char* words = tr(STR_TIP_DONT_SHOW);
  const int font = r.getTextWidth(UI_12_FONT_ID, words) <= button.w - 24 ? UI_12_FONT_ID : SMALL_FONT_ID;
  const std::string text = r.truncatedText(font, words, button.w - 24);
  r.drawText(font, button.x + (button.w - r.getTextWidth(font, text.c_str())) / 2,
             button.y + (button.h - r.getLineHeight(font)) / 2, text.c_str());
}
