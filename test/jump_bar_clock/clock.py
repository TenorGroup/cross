#!/usr/bin/env python3
"""The way back's bar counts 3, 2, 1, each number a second from the moment the refresh that showed it has finished.

Compiles src/activities/reader/JumpBarClock.h (or --header) and plays the reader's loop against a clock with slow refreshes:
no refresh may eat into a number's second, the bar stands 3 seconds at least, a repaint of its own page carries it on with the
number it had, any other page takes it down.
"""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--header', type=Path)
args = parser.parse_args()
header = args.header or args.repo / 'src/activities/reader/JumpBarClock.h'
args.output.mkdir(parents=True, exist_ok=True)
source = args.output / 'clock.cpp'
source.write_text(r'''
#include <cassert>
#include <vector>
#include "JumpBarClock.h"
using namespace jumpbar;

// The reader's step: what handleJumpBar does with the clock each pass. Returns the refresh time it asked of the panel.
struct Run {
  Clock clock;
  unsigned long now = 0;
  std::vector<unsigned long> shownAt;  // when each number's refresh finished
  unsigned long hiddenAt = 0;
  // One pass at `now`; `refreshMs` is what a ring refresh takes on this panel.
  void pass(unsigned long refreshMs) {
    switch (clock.due(now)) {
      case Due::Wait: return;
      case Due::Hide: hiddenAt = now; clock.drop(); return;
      case Due::Next:
        clock.next();
        now += refreshMs;          // the refresh returns when the panel has finished
        clock.onPanel(now);
        shownAt.push_back(now);
        return;
    }
  }
  void until(unsigned long end, unsigned long refreshMs) {
    for (; now < end && clock.up(); now += 10) pass(refreshMs);
  }
};

int main() {
  // 1. Nothing is due before the page that carries the bar is on the panel, however late the pass is.
  {
    Clock c; c.arm(1, 4);
    assert(c.up() && c.digit == 3 && c.due(999999) == Due::Wait);
    c.onPanel(5000);
    assert(c.due(5999) == Due::Wait && c.due(6000) == Due::Next);
  }
  // 2. Slow refreshes (0.7 s, 1.7 s, 2.5 s): each number still stands a second after its refresh, 3 numbers in order.
  for (unsigned long refresh : {0ul, 100ul, 700ul, 1700ul, 2500ul}) {
    Run r; r.clock.arm(1, 4); r.now = 5000; r.clock.onPanel(5000);
    r.until(60000, refresh);
    assert(!r.clock.up() && r.hiddenAt);
    assert(r.shownAt.size() == 2);                       // the 2 and the 1; the 3 came with the page
    assert(r.shownAt[0] - 0 >= 5000 + 1000);             // the 2 is on after the 3 stood a second
    assert(r.shownAt[1] - refresh >= r.shownAt[0] + 1000);  // the 1 is started after the 2 stood a second
    assert(r.hiddenAt >= r.shownAt[1] + 1000);           // the 1 stood a second
    assert(r.hiddenAt - 5000 >= 3000 + 2 * refresh - 20);   // 3 seconds on the panel at least, refreshes not counted
  }
  // 3. A repaint of the page the bar stands on carries it on with the number it had and counts that number again.
  {
    Clock c; c.arm(1, 4); c.onPanel(5000); c.next(); c.onPanel(6700);        // the 2 is on since 6700
    assert(c.stands(1, 4));
    assert(c.due(7699) == Due::Wait);
    c.repainting();                                                           // a repaint begins
    assert(c.digit == 2 && c.due(100000) == Due::Wait);                      // still the 2, not due until it is on again
    c.onPanel(8300);
    assert(c.due(9299) == Due::Wait && c.due(9300) == Due::Next);
  }
  // 4. Another page, another chapter, or none: not the bar's.
  {
    Clock c; c.arm(1, 4); c.onPanel(5000);
    assert(!c.stands(1, 5) && !c.stands(2, 4) && c.stands(1, 4));
    c.drop();
    assert(!c.up() && !c.stands(1, 4) && c.due(99999) == Due::Wait);
  }
  // 5. A new jump counts again from 3.
  {
    Clock c; c.arm(1, 4); c.onPanel(5000); c.next(); c.next();
    c.arm(2, 0);
    assert(c.digit == 3 && c.shownAt == 0 && c.spine == 2 && c.page == 0);
  }
  return 0;
}
''')
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', f'-I{header.parent}',
                str(source), '-o', str(args.output / 'clock')], check=True, **({} if not args.header else {}))
subprocess.run([str(args.output / 'clock')], check=True)
print('GREEN jump bar clock: 5 cases, refreshes of 0 to 2500 ms')
