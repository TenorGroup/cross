#include <cassert>
#include <cstdio>
#ifdef SHEET_RADIO_PLAN_BASELINE
// v1.0.55 behaviour, written out: the sheet always stops the radio and stores the snapshot.
namespace sheetradio {
struct Plan {
  bool stopRadio;
  bool storeSnapshot;
};
constexpr Plan planFor(bool, bool) { return Plan{true, true}; }
constexpr bool redrawFromPage(bool, bool) { return false; }
}  // namespace sheetradio
#else
#include "activities/reader/SheetRadioPlan.h"
#endif

int main() {
  using sheetradio::planFor;
  using sheetradio::redrawFromPage;
  // Release builds (keepRadio false) behave as v1.0.55 whatever the radio does.
  assert(planFor(false, false).stopRadio && planFor(false, false).storeSnapshot);
  assert(planFor(false, true).stopRadio && planFor(false, true).storeSnapshot);
  // P5b with the radio off: nothing to keep, the sheet stores its snapshot as before.
  assert(planFor(true, false).stopRadio && planFor(true, false).storeSnapshot);
  // P5b with the radio up: the remote stays linked and the 52 KB snapshot is not taken.
  assert(!planFor(true, true).stopRadio);
  assert(!planFor(true, true).storeSnapshot);
  // Without a snapshot the sheet is redrawn over a freshly drawn page; with one, it is restored.
  assert(redrawFromPage(true, false));
  assert(!redrawFromPage(true, true));
  assert(!redrawFromPage(false, false));
  std::puts("sheet_radio_plan: 8 checks passed");
  return 0;
}
