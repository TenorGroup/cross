// The tap tip draws the zones zoneAt decides: every box it draws answers zoneAt with its own zone.
#include "../../src/activities/reader/ReaderTapZones.h"

#include <cassert>

using readertap::Box;
using readertap::Zone;

namespace {
// Points of a box that are not under the centre cell, where zoneAt must answer `z`.
void agrees(const Zone z, const int w, const int h, const readertap::Rules& r) {
  const Box b = readertap::zoneBox(z, w, h, r);
  assert(b.w > 0 && b.h > 0);
  const Box cell = readertap::zoneBox(Zone::Menu, w, h, r);
  int checked = 0;
  for (int y = b.y; y < b.y + b.h; y += 7)
    for (int x = b.x; x < b.x + b.w; x += 7) {
      if (z != Zone::Menu && cell.contains(x, y)) continue;
      assert(readertap::zoneAt(x, y, w, h, r) == z);
      ++checked;
    }
  assert(checked > 0);
}
}  // namespace

int main() {
  for (const bool landscape : {false, true}) {
    const int w = landscape ? 800 : 480, h = landscape ? 480 : 800;
    for (uint8_t step = 0; step < readertap::BACK_ZONE_COUNT; ++step)
      for (const bool inverted : {false, true}) {
        const readertap::Rules r{true, true, inverted, true, true, step};
        for (const Zone z : {Zone::Prev, Zone::Next, Zone::Menu, Zone::TopMenu, Zone::TextMenu}) agrees(z, w, h, r);
        // The button sits in the forward zone, clear of the centre cell and the bands, at least 48 px tall.
        const Box button = readertap::tipButton(w, h, r);
        assert(button.h >= 48 && button.w >= 200);
        for (const int x : {button.x, button.x + button.w - 1})
          for (const int y : {button.y, button.y + button.h - 1})
            assert(readertap::zoneAt(x, y, w, h, r) == Zone::Next);
      }
  }
  // Portrait, 25%: the column the setting names.
  const readertap::Rules r{true, true, false, true, true, readertap::BACK_ZONE_DEFAULT};
  const Box back = readertap::zoneBox(Zone::Prev, 480, 800, r);
  assert(back.x == 0 && back.y == 100 && back.w == 120 && back.h == 638);
  // One direction on taps: it takes the body, the other draws nothing.
  const readertap::Rules nextOnly{true, false, false, true, true, readertap::BACK_ZONE_DEFAULT};
  assert(readertap::zoneBox(Zone::Prev, 480, 800, nextOnly).w == 0);
  agrees(Zone::Next, 480, 800, nextOnly);
  return 0;
}
