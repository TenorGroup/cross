#include <cassert>
#include <cstdio>

#include "HeapBigProbe.h"

int main() {
  using heapbig::radioTaskName;
  using heapbig::record;
  // Every allocation from 4 KB up.
  assert(record(4096));
  assert(record(61440));
  assert(!record(4095));
  // While the reader holds the grayscale strip: another task's allocation from 1 KB up, or any that failed.
  assert(record(1024, true));
  assert(!record(1023, true));
  assert(record(16, true, false));
  assert(!record(1024, false));
  // The floor: both numbers must hold.
  assert(!heapfloor::crosses(10240, 8192));
  assert(heapfloor::crosses(10239, 8192));
  // An 8,192 B block reports 8,180 with light poisoning: it still serves the strip, so not under.
  assert(!heapfloor::crosses(10240, 8180));
  assert(heapfloor::crosses(10240, 8179));
  // The radio's tasks by name; the reader's tasks are not.
  assert(radioTaskName("ble-start") && radioTaskName("ble-conn") && radioTaskName("nimble_host"));
  assert(radioTaskName("btController") && radioTaskName("BTC"));
  assert(!radioTaskName("loopTask") && !radioTaskName("ActivityManagerRender") && !radioTaskName("fi_input"));
  assert(!radioTaskName(nullptr));
  std::puts("heap_big_probe: 15 checks passed");
  return 0;
}
