// Where a tap on a reader page goes: the one function every reader asks (readertap::zoneAt).
#include "../../src/activities/reader/ReaderTapZones.h"

#include <cassert>

using readertap::Zone;

namespace {
readertap::Rules tapAndSwipe(const bool bands, const uint8_t backZone = readertap::BACK_ZONE_DEFAULT) {
  return {true, true, false, true, bands, backZone};
}
}  // namespace

int main() {
  // Portrait 480 x 800, the touch shell's page: KOReader's 25% back column, the rest goes forward.
  const auto r = tapAndSwipe(true);
  assert(readertap::topBand(800) == 100 && readertap::footBand(800) == 62);
  assert(zoneAt(0, 0, 480, 800, r) == Zone::TopMenu);
  assert(zoneAt(479, 99, 480, 800, r) == Zone::TopMenu);
  assert(zoneAt(0, 100, 480, 800, r) == Zone::Prev);
  assert(zoneAt(119, 400, 480, 800, r) == Zone::Prev);
  assert(zoneAt(120, 400, 480, 800, r) == Zone::Next);
  assert(zoneAt(150, 400, 480, 800, r) == Zone::Next);  // the old 1/3 split sent this back
  assert(zoneAt(479, 737, 480, 800, r) == Zone::Next);
  assert(zoneAt(0, 737, 480, 800, r) == Zone::Prev);
  assert(zoneAt(240, 738, 480, 800, r) == Zone::TextMenu);
  assert(zoneAt(479, 799, 480, 800, r) == Zone::TextMenu);
  // The middle of a page with bands turns forward (founder 06/10, KOReader's map): the menu is the foot band's.
  assert(zoneAt(240, 400, 480, 800, r) == Zone::Next);
  assert(zoneAt(160, 266, 480, 800, r) == Zone::Next);
  assert(zoneAt(319, 533, 480, 800, r) == Zone::Next);
  // Outside the page: nothing.
  assert(zoneAt(-1, 400, 480, 800, r) == Zone::None && zoneAt(480, 400, 480, 800, r) == Zone::None);

  // Landscape 800 x 480: the bands keep a 60 px target.
  assert(readertap::topBand(480) == 60 && readertap::footBand(480) == 60);
  assert(zoneAt(400, 59, 800, 480, r) == Zone::TopMenu);
  assert(zoneAt(199, 100, 800, 480, r) == Zone::Prev);
  assert(zoneAt(200, 100, 800, 480, r) == Zone::Next);
  assert(zoneAt(400, 420, 800, 480, r) == Zone::TextMenu);
  assert(zoneAt(400, 240, 800, 480, r) == Zone::Next);
  assert(zoneAt(700, 419, 800, 480, r) == Zone::Next);

  // The setting's four widths: 15, 20, 25, 33% of the width.
  const int edges[] = {72, 96, 120, 158};
  for (uint8_t step = 0; step < 4; ++step) {
    const auto s = tapAndSwipe(true, step);
    assert(zoneAt(edges[step] - 1, 200, 480, 800, s) == Zone::Prev);
    assert(zoneAt(edges[step], 200, 480, 800, s) == Zone::Next);
  }
  // A stored value nothing wrote falls back to 25%.
  assert(zoneAt(119, 200, 480, 800, tapAndSwipe(true, 9)) == Zone::Prev);
  assert(zoneAt(120, 200, 480, 800, tapAndSwipe(true, 9)) == Zone::Next);

  // Readers that do not handle the bands keep the whole height for turning.
  const auto plain = tapAndSwipe(false);
  assert(zoneAt(0, 0, 480, 800, plain) == Zone::Prev && zoneAt(300, 799, 480, 800, plain) == Zone::Next);

  // Inverted tap and right-to-left books mirror the back column to the right edge.
  readertap::Rules mirrored{true, true, true, true, true, readertap::BACK_ZONE_DEFAULT};
  assert(zoneAt(0, 400, 480, 800, mirrored) == Zone::Next);
  assert(zoneAt(359, 400, 480, 800, mirrored) == Zone::Next);
  assert(zoneAt(360, 400, 480, 800, mirrored) == Zone::Prev);

  // One direction on taps takes the whole page; neither leaves taps to the menus alone.
  readertap::Rules nextOnly{true, false, false, true, true, readertap::BACK_ZONE_DEFAULT};
  assert(zoneAt(0, 400, 480, 800, nextOnly) == Zone::Next);
  readertap::Rules prevOnly{false, true, false, true, true, readertap::BACK_ZONE_DEFAULT};
  assert(zoneAt(479, 400, 480, 800, prevOnly) == Zone::Prev);
  readertap::Rules swipeOnly{false, false, false, true, true, readertap::BACK_ZONE_DEFAULT};
  assert(zoneAt(0, 400, 480, 800, swipeOnly) == Zone::None);
  assert(zoneAt(240, 400, 480, 800, swipeOnly) == Zone::None);
  // A reader without bands (TXT, XTC) keeps the centre as its way into the menu.
  readertap::Rules noBands{true, true, false, true, false, readertap::BACK_ZONE_DEFAULT};
  assert(zoneAt(240, 400, 480, 800, noBands) == Zone::Menu);
  assert(zoneAt(150, 400, 480, 800, noBands) == Zone::Next);
  assert(zoneAt(240, 10, 480, 800, swipeOnly) == Zone::TopMenu);
  // The menu tap switched off gives the centre to the turn zones.
  readertap::Rules noMenu{true, true, false, false, true, readertap::BACK_ZONE_DEFAULT};
  assert(zoneAt(240, 400, 480, 800, noMenu) == Zone::Next);
  return 0;
}
