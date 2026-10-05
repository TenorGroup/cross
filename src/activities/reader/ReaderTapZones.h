#pragma once

#include <cstdint>

// Where a tap on a reader's page goes. The one place that decides it: the page turn, the reader menu and
// the touch shell's bands all ask zoneAt, so the zones can never overlap or leave a gap between callers.
// The layout follows KOReader's defaults (defaults.lua: DTAP_ZONE_BACKWARD w 1/4, DTAP_ZONE_FORWARD the
// rest, DTAP_ZONE_MENU the top 1/8), kept with Tenor's centre tap for the reader menu (dynamic bar rule 4).
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
  bool menuTap;      // the centre third opens the reader menu
  bool bands;        // the caller handles the top band (top menu) and the foot band (text menu)
  uint8_t backZone;  // index into BACK_PERCENT
};

// The top band: 1/8 of the height (100 px upright, 60 px on its side).
inline int topBand(const int height) { return height / 8; }
// The foot band holds the title, clock and battery: 1/13 of the height (KOReader's minibar), at least the
// 60 px (7 mm) a thumb hits.
inline int footBand(const int height) {
  const int band = (height + 12) / 13;
  return band < 60 ? 60 : band;
}

inline Zone zoneAt(const int x, const int y, const int width, const int height, const Rules& r) {
  if (x < 0 || y < 0 || x >= width || y >= height) return Zone::None;
  if (r.bands && y < topBand(height)) return Zone::TopMenu;
  if (r.bands && y >= height - footBand(height)) return Zone::TextMenu;
  if (r.menuTap && x >= width / 3 && x < width - width / 3 && y >= height / 3 && y < height - height / 3)
    return Zone::Menu;
  if (!r.nextTaps && !r.prevTaps) return Zone::None;
  // Only one direction on taps: it takes the whole page.
  if (!r.prevTaps) return Zone::Next;
  if (!r.nextTaps) return Zone::Prev;
  const int back = width * BACK_PERCENT[r.backZone < BACK_ZONE_COUNT ? r.backZone : BACK_ZONE_DEFAULT] / 100;
  const bool forward = r.inverted ? x < width - back : x >= back;
  return forward ? Zone::Next : Zone::Prev;
}

}  // namespace readertap
