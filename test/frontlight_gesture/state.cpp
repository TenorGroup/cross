#include "../../src/components/FrontlightGesture.h"
#include <cassert>
#include <limits>
int main() {
  // Brightness follows the two fingers while they are down: 5% per 30 px, shown from the first step.
  FrontlightGesture s;
  assert(!s.follow(240, 400, 50, 50, true, 100));  // fingers down: nothing yet
  assert(!s.follow(240, 380, 50, 50, true, 110) && !s.visible);
  assert(s.follow(240, 370, 50, 50, true, 120));
  assert(s.vertical && s.value == 55 && s.visible && s.changedAt == 120);
  assert(!s.follow(240, 360, 55, 50, true, 130));  // still the same step
  assert(s.follow(240, 340, 55, 50, true, 140) && s.value == 60);
  assert(s.follow(240, 430, 60, 50, true, 150) && s.value == 45);  // back below where it started
  assert(s.follow(240, 2000, 45, 50, true, 160) && s.value == 0);
  assert(s.follow(240, -2000, 0, 50, false, 170) && s.value == 100);
  s.lift();
  assert(!s.expired(1669) && s.expired(1670));

  // Off with a level kept (a fast flick, the panel, the power key): the first step up brings that level back,
  // the next ones add 5%.
  FrontlightGesture r;
  assert(!r.follow(100, 500, 40, 50, false, 0));
  assert(r.follow(100, 470, 40, 50, false, 10) && r.value == 40);
  assert(r.follow(100, 440, 40, 50, true, 20) && r.value == 45);
  assert(r.follow(100, 500, 45, 50, true, 30) && r.value == 0);  // back to the start: off again
  r.lift();
  // Off with nothing kept starts at 5%; a swipe down from off stays off.
  FrontlightGesture z;
  assert(!z.follow(100, 500, 0, 50, false, 0));
  assert(z.follow(100, 470, 0, 50, false, 10) && z.value == 5);
  FrontlightGesture d;
  assert(!d.follow(100, 500, 40, 50, false, 0));
  assert(d.follow(100, 560, 40, 50, false, 10) && d.value == 0);

  // Sideways: warmth, right warmer. A diagonal locks no axis.
  FrontlightGesture w;
  assert(!w.follow(100, 400, 50, 30, true, 0));
  assert(!w.follow(125, 425, 50, 30, true, 10));
  assert(!w.follow(140, 430, 50, 30, true, 20));
  assert(w.follow(160, 400, 50, 30, true, 30) && !w.vertical && w.value == 40);
  assert(!w.follow(160, 300, 50, 40, true, 40) && !w.vertical);  // the axis stays locked
  w.lift();

  // A fast flick down puts the light out at once and keeps the level the fingers started from.
  FrontlightGesture f;
  assert(!f.follow(240, 300, 70, 50, true, 0));
  assert(f.follow(240, 420, 70, 50, true, 100) && f.value == 50);
  f.lift();
  assert(f.flick(0, 120, 200, 50, 200));  // 0.6 px/ms: the slowest flick
  assert(f.keep == 70 && f.value == 0 && f.vertical && f.visible && f.changedAt == 200);
  // Slower, shorter, upwards or sideways: no flick, the steps already applied stand.
  FrontlightGesture g;
  assert(!g.flick(0, 120, 201, 50, 0));
  assert(!g.flick(0, 119, 50, 50, 0));
  assert(!g.flick(0, -300, 100, 50, 0));
  assert(!g.flick(150, 150, 100, 50, 0));
  // A flick the live path never saw (fingers too fast for a frame) keeps the level it found.
  FrontlightGesture h;
  assert(h.flick(10, 300, 100, 33, 0) && h.keep == 33);
  // A later flick does not reuse an old gesture's start.
  assert(!f.follow(240, 300, 20, 50, true, 300));
  assert(f.follow(240, 330, 20, 50, true, 310) && f.value == 15);
  f.lift();
  assert(f.flick(0, 300, 100, 15, 320) && f.keep == 20);
  assert(f.flick(0, 300, 100, 15, 1000) && f.keep == 15);

  // A release no live frame showed steps by its whole travel; one that was followed live does not again.
  FrontlightGesture q;
  assert(q.settle(0, -120, 250, 60, 50, false, 0) && q.vertical && q.value == 75);
  assert(q.settle(120, 0, 250, 60, 50, true, 0) && !q.vertical && q.value == 70);
  assert(!q.settle(0, -10, 250, 60, 50, true, 0));
  assert(!q.follow(0, 500, 60, 50, true, 1000) && q.follow(0, 440, 60, 50, true, 1010));
  q.lift();
  assert(!q.settle(0, -60, 30, 70, 50, true, 1020));
  // A live gesture the controller never classified leaves no claim on the next release.
  assert(q.settle(0, -60, 250, 70, 50, true, 5000) && q.value == 80);

  FrontlightGesture e;
  e.visible = true;
  e.changedAt = std::numeric_limits<uint32_t>::max() - 1000;
  assert(!e.expired(498) && e.expired(499));
}
