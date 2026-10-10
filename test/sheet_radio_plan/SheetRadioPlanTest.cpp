#include <cassert>
#include <cstdio>
#ifdef SHEET_RADIO_PLAN_BASELINE
// v1.0.57 behaviour, written out: the sheet always stops the radio and stores the snapshot.
namespace sheetradio {
struct Plan {
  bool stopRadio;
  bool storeSnapshot;
};
constexpr Plan planFor(bool) { return Plan{true, true}; }
constexpr bool redrawFromPage(bool, bool) { return false; }
}  // namespace sheetradio
#else
#include "activities/reader/SheetRadioPlan.h"
#endif

int main() {
  using sheetradio::planFor;
  using sheetradio::redrawFromPage;
  // Radio off: nothing to keep, the sheet stops nothing new and stores its snapshot as before.
  assert(planFor(false).stopRadio && planFor(false).storeSnapshot);
  // Radio up: the remote stays linked and the 52 KB snapshot is not taken.
  assert(!planFor(true).stopRadio);
  assert(!planFor(true).storeSnapshot);
  // Without a snapshot the sheet is redrawn over a freshly drawn page; with one, it is restored.
  assert(redrawFromPage(true, false));
  assert(!redrawFromPage(true, true));
  assert(!redrawFromPage(false, false));
  std::puts("sheet_radio_plan: 7 checks passed");
  return 0;
}
