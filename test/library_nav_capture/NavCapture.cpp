#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>

struct Navigator {
  int action = -1;
  size_t largestCapture = 0;
  template <class Callback>
  void run(int kind, Callback callback) {
    largestCapture = std::max(largestCapture, sizeof(callback));
    std::function<void()> wrapped(callback);
    if (kind == action) wrapped();
  }
  template <class F> void onNextRelease(F f) { run(0, f); }
  template <class F> void onPreviousRelease(F f) { run(1, f); }
  template <class F> void onNextContinuous(F f) { run(2, f); }
  template <class F> void onPreviousContinuous(F f) { run(3, f); }
  static int nextPageIndex(int at, int count, int rows) { return std::min(count - 1, at + rows); }
  static int previousPageIndex(int at, int, int rows) { return std::max(0, at - rows); }
};
using ButtonNavigator = Navigator;
class LibraryListActivity {
 public:
  struct Nav { int pageRows() const { return 3; } } nav;
  Navigator buttonNavigator;
  bool degraded = false;
  int ring = 2, count = 10, tabStep = 0, searches = 0;
  int listCount() const { return count; }
  Nav& activeNav() { return nav; }
  int ringPos() const { return ring; }
  int selectedEntry() const { return ring - 1; }
  bool tabsFocused() const { return ring == 0; }
  void moveRingTo(int value) { ring = value; }
  void openSearch() { ++searches; }
  void stepTab(int direction) { tabStep += direction; }
  void navigateButtons();
};
#include "navigation.inc"

int main() {
  size_t largest = 0;
  for (int action = 0; action < 4; ++action) {
    LibraryListActivity rows;
    rows.buttonNavigator.action = action;
    rows.navigateButtons();
    const int expected[] = {3, 1, 5, 1};
    assert(rows.ring == expected[action] && rows.searches == 0 && rows.tabStep == 0);
    LibraryListActivity tabs;
    tabs.ring = 0;
    tabs.buttonNavigator.action = action;
    tabs.navigateButtons();
    assert(tabs.searches == (action == 1));
    assert(tabs.tabStep == (action == 2 ? 1 : action == 3 ? -1 : 0));
    largest = std::max(largest, rows.buttonNavigator.largestCapture);
  }
  LibraryListActivity empty;
  empty.count = 0;
  empty.buttonNavigator.action = 2;
  empty.navigateButtons();
  assert(empty.ring == 2);
  printf("9/9 navigation cases preserved, largest callback %zu B, limit %zu B\n", largest, 2 * sizeof(void*));
  assert(largest <= 2 * sizeof(void*));
}
