#pragma once

#include <algorithm>
#include <cstdint>

// Where a tap on a reader's page goes. The one place that decides it: the page turn, the reader menu and
// the touch shell's bands all ask zoneAt, so the zones can never overlap or leave a gap between callers.
// The layout follows KOReader's defaults (defaults.lua: DTAP_ZONE_BACKWARD w 1/4, DTAP_ZONE_FORWARD the
// rest, DTAP_ZONE_MENU the top 1/8). A page with bands has no centre cell: its middle turns forward and the
// menus are the bands' (founder 06/10). A reader without bands keeps the centre tap into its menu.
namespace readertap {

enum class Zone : uint8_t { None, Prev, Next, Menu, TopMenu, TextMenu };

// The back column's width, the setting's four steps in percent of the page width.
inline constexpr uint8_t BACK_PERCENT[] = {15, 20, 25, 33};
inline constexpr uint8_t BACK_ZONE_COUNT = sizeof(BACK_PERCENT);
inline constexpr uint8_t BACK_ZONE_DEFAULT = 2;  // 25%, KOReader's

struct Rules {
  bool nextTaps;     // a tap may turn forward
  bool prevTaps;     // a tap may turn back
  bool inverted;     // the back column sits at the right edge (inverted tap, or a right-to-left book)
  bool menuTap;      // the centre third opens the reader menu (readers without bands only)
  bool bands;        // the caller handles the top band (top menu) and the foot band (text menu)
  uint8_t backZone;  // index into BACK_PERCENT
};

// The foot band holds the title, clock and battery: 1/13 of the height (KOReader's minibar), at least the
// 60 px (7 mm) a thumb hits.
inline int footBand(const int height) {
  const int band = (height + 12) / 13;
  return band < 60 ? 60 : band;
}
// The top band is as tall as the foot band (founder 06/10): 62 px upright, 60 px on its side.
inline int topBand(const int height) { return footBand(height); }

inline int backWidth(const int width, const Rules& r) {
  return width * BACK_PERCENT[r.backZone < BACK_ZONE_COUNT ? r.backZone : BACK_ZONE_DEFAULT] / 100;
}

inline Zone zoneAt(const int x, const int y, const int width, const int height, const Rules& r) {
  if (x < 0 || y < 0 || x >= width || y >= height) return Zone::None;
  if (r.bands && y < topBand(height)) return Zone::TopMenu;
  if (r.bands && y >= height - footBand(height)) return Zone::TextMenu;
  if (r.menuTap && !r.bands && x >= width / 3 && x < width - width / 3 && y >= height / 3 && y < height - height / 3)
    return Zone::Menu;
  if (!r.nextTaps && !r.prevTaps) return Zone::None;
  // Only one direction on taps: it takes the whole page.
  if (!r.prevTaps) return Zone::Next;
  if (!r.nextTaps) return Zone::Prev;
  const int back = backWidth(width, r);
  const bool forward = r.inverted ? x < width - back : x >= back;
  return forward ? Zone::Next : Zone::Prev;
}

struct Box {
  int x, y, w, h;
  bool contains(const int px, const int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// The box a zone covers, for the tap tip to draw (the centre cell overlaps the turn zones and wins taps
// there). Empty for a zone these rules do not have.
inline Box zoneBox(const Zone z, const int width, const int height, const Rules& r) {
  const int top = r.bands ? topBand(height) : 0;
  const int bottom = r.bands ? height - footBand(height) : height;
  const int back = r.nextTaps && r.prevTaps ? backWidth(width, r) : (r.prevTaps ? width : 0);
  const int backX = r.inverted ? width - back : 0;
  switch (z) {
    case Zone::TopMenu:
      return r.bands ? Box{0, 0, width, top} : Box{0, 0, 0, 0};
    case Zone::TextMenu:
      return r.bands ? Box{0, bottom, width, height - bottom} : Box{0, 0, 0, 0};
    case Zone::Menu:
      return r.menuTap && !r.bands ? Box{width / 3, height / 3, width - 2 * (width / 3), height - 2 * (height / 3)}
                                   : Box{0, 0, 0, 0};
    case Zone::Prev:
      return {backX, top, back, back ? bottom - top : 0};
    case Zone::Next:
      if (!r.nextTaps) return {0, 0, 0, 0};
      return {r.inverted ? 0 : back, top, width - back, bottom - top};
    default:
      return {0, 0, 0, 0};
  }
}

// The tip's "do not show again" button: in the forward zone, in the band between 2/3 of the height and the
// foot band.
inline Box tipButton(const int width, const int height, const Rules& r) {
  const Box next = zoneBox(Zone::Next, width, height, r);
  const int cellBottom = height - height / 3;
  const int bottom = next.y + next.h;
  const int h = std::min(60, bottom - cellBottom - 16);
  const int w = std::min(next.w - 32, 320);
  return {next.x + (next.w - w) / 2, cellBottom + (bottom - cellBottom - h) / 2, w, h};
}

}  // namespace readertap
