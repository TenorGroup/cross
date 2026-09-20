#include <cassert>
#include <cstdio>
#include "components/HomeStatsNavigation.h"
int main() {
 for (int action = 0; action < 6; ++action) {
  int ring = action + 1;
  for (int cycle = 0; cycle < 50; ++cycle) {
#ifdef LEGACY_METRICS
    int larger = ring;
#else
    int larger = statsSelectionForTier(ring, false, true);
#endif
    assert(statsActionIndex(larger - 1, true) == action);
    ring = statsSelectionForTier(larger, true, false);
    assert(statsActionIndex(ring - 1, false) == action);
  }
 }
 assert(statsSelectionForTier(0, false, true) == 0);
 assert(statsSelectionForTier(1, true, false) == 1);
 puts("PASS: six stats actions retain identity through50 size cycles and page row removal");
}
