#include "../../src/components/FrontlightGesture.h"
#include <cassert>
#include <limits>
int main() {
  FrontlightGesture s;
  for (int contacts : {1, 3, 4}) {
    assert(!s.apply(contacts, 0, -60, 50, 50, true, 100));
    assert(!s.visible && !s.dirty);
  }
  assert(!s.apply(2, 0, -5, 50, 50, true, 100));
  assert(s.apply(2, 0, -60, 60, 50, false, 100));
  assert(s.vertical && s.value == 10 && s.dirty);
  assert(!s.expired(1599) && s.expired(1600));
  assert(s.apply(2, 0, 60, 10, 50, true, 1700) && s.value == 0);
  assert(s.apply(2, 0, -6000, 0, 50, false, 1800) && s.value == 100);
  assert(s.apply(2, 60, 0, 30, 50, false, 1900) && !s.vertical && s.value == 60);
  assert(s.apply(2, -6000, 0, 30, 60, true, 2000) && s.value == 0);
  assert(s.apply(2, 6000, 0, 30, 0, true, 2100) && s.value == 100);
  s.changedAt = std::numeric_limits<uint32_t>::max() - 1000;
  assert(!s.expired(498) && s.expired(499));
}
