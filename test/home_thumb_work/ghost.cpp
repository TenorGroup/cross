#include <cassert>
#include <cstdio>
#include "src/activities/home/HomeRefresh.h"

int main() {
  using Mode = HomeRefresh::Mode;
  HomeRefresh refresh;
  refresh.painted(true, 0, 0, 100);
  assert(!refresh.idle(10000, true, false));
  for (int n = 1; n <= 20; ++n) {
    const uint32_t now = 10000 + n * 300;
    assert(refresh.choose(true, n % 7, 0).mode == Mode::Window);
    refresh.painted(true, n % 7, 0, now);
    assert(!refresh.idle(now + 299, true, false));
    if (n < 10) assert(!refresh.idle(now + 2000, true, false));
  }
  assert(refresh.count() == 20);
  assert(!refresh.idle(17999, true, false));
  assert(!refresh.idle(18000, true, true));
  assert(!refresh.idle(19999, true, false));
  assert(refresh.idle(20000, true, false));
  refresh.restored();
  assert(!refresh.idle(30000, true, false));
  assert(refresh.count() == 0);
  refresh.painted(true, 0, 0, 30000);
  assert(refresh.count() == 1);
  assert(refresh.choose(false, 0, 0).mode == Mode::Restore);
  refresh.restored();
  refresh.leave();
  assert(refresh.choose(false, 0, 0).mode == Mode::Normal);
  assert(refresh.choose(true, 0, 0).mode == Mode::Normal);
  refresh.painted(true, 0, 0, UINT32_MAX - 500);
  for (int n = 1; n <= 10; ++n) refresh.painted(true, n % 7, 0, UINT32_MAX - 500);
  assert(!refresh.idle(1498, true, false));
  assert(refresh.idle(1499, true, false));
  assert(!refresh.idle(1499, false, false));
  assert(!refresh.idle(3498, true, false));
  assert(refresh.idle(3499, true, false));
  std::printf("GREEN: threshold=10, 20 continuous swaps, input, leave, wrap; state=%zu B\n", sizeof(HomeRefresh));
}
