#include <cassert>
#include <cstdio>
#include "src/activities/home/HomeRefresh.h"

int main() {
  using Mode = HomeRefresh::Mode;
  for (unsigned style = 0; style < 3; ++style) {
    HomeRefresh refresh;
    auto plan = refresh.choose(true, 0, style);
    assert(plan.oneBit && plan.mode == Mode::Normal && !plan.changedBook);
    refresh.painted(true, 0, style);
    for (int n = 1; n <= 20; ++n) {
      const int book = n % 7;
      plan = refresh.choose(true, book, style);
      assert(plan.oneBit && plan.mode == Mode::Window && plan.changedBook);
      refresh.painted(true, book, style);
    }
    plan = refresh.choose(true, 6, style);
    assert(plan.mode == Mode::Normal && !plan.changedBook);
    assert(refresh.choose(true, 6, style + 16).mode == Mode::Normal);
    assert(!refresh.choose(false, 6, style).oneBit);
    refresh.leave();
    assert(refresh.choose(true, 6, style).mode == Mode::Normal);
  }
  std::printf("GREEN: 60 swaps use one BW window; state=%zu B\n", sizeof(HomeRefresh));
}
