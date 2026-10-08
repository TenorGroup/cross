#include <cassert>
#include <cmath>
#include <cstdio>

#include "activities/reader/ReaderPace.h"

using P = readerstatus::Pace;
P::Position pos(int page) { return {0, page, 10, page / 10.0f}; }

int main() {
  P p;
  uint32_t seconds = 0;
  assert(!p.estimate(pos(0), true, seconds));
  for (int page = 0; page < 3; ++page) {
    p.shown(1000 + page * 30000, pos(page), 1);
    assert(p.turned(31000 + page * 30000, true, pos(page + 1)));
  }
  assert(p.samples() == 3);
  assert(p.estimate(pos(3), false, seconds) && seconds == 210);
  assert(p.estimate(pos(3), true, seconds) && std::abs(static_cast<int>(seconds) - 210) <= 1);
  p.shown(100000, pos(3), 1);
  p.shown(115000, pos(3), 1); // A footer refresh retains the first paint's timer.
  assert(p.turned(130000, true, pos(4)));
  assert(p.samples() == 4);
  p.shown(140000, pos(4), 1);
  assert(!p.turned(141000, true, pos(5))); // A quick browse is excluded.
  p.shown(150000, pos(5), 1);
  assert(!p.turned(400000, true, pos(6))); // Idle is excluded.
  p.shown(410000, pos(6), 1);
  assert(!p.turned(440000, false, pos(5))); // Backward turn is excluded.
  p.shown(450000, pos(5), 1);
  assert(!p.turned(480000, true, pos(8))); // Jump is excluded.
  p.shown(490000, pos(8), 1);
  p.suspend();
  assert(!p.turned(520000, true, pos(9))); // A menu/sleep suspends the timer.
  assert(p.samples() == 4);
  p.shown(600000, pos(8), 1);
  assert(p.turned(630000, true, pos(9)));
  assert(!p.turned(660000, true, {1, 0, 5, 0.95f})); // No painted page between turns.
  p.shown(700000, pos(9), 1);
  assert(p.turned(730000, true, {1, 0, 5, 1.0f})); // Adjacent chapter boundary counts.
  p.shown(740000, pos(0), 2);
  assert(p.samples() == 0 && !p.estimate(pos(0), true, seconds)); // Reflow resets the pace.
  assert(!p.estimate({0, 0, 0, 0}, true, seconds));
  p.suspend();
  p.shown(UINT32_MAX - 10000, pos(0), 2);
  assert(p.turned(19999, true, pos(1))); // Duration survives millis wrap.
  std::puts("reader_pace:GREEN (sample guards, pause, jumps, reflow, remaining time, wrap)");
}
