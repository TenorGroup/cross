#include <cassert>
#include <cstdio>

#ifdef HEAP_FLOOR_BASELINE
// v1.0.57 written out: no door, every allocation stands whatever it leaves behind.
#include <cstddef>
namespace heapfloor {
constexpr bool stands(size_t, size_t, size_t) { return true; }
}  // namespace heapfloor
#else
#include <HeapFloor.h>
#endif

int main() {
  using heapfloor::stands;
  // From 4 KB up, an allocation stands only when the heap left is on the floor: free 10,240 B and a
  // block of 8,192 B, which the heap API reports as 8,180.
  assert(stands(4096, 10240, 8180));
  assert(!stands(4096, 10239, 20000));
  assert(!stands(32768, 40000, 8179));
  assert(!stands(61440, 1732, 1000));
  // Below 4 KB the door does not judge.
  assert(stands(4095, 0, 0));
#ifndef HEAP_FLOOR_BASELINE
  // The grayscale strip Home and reader share: 80 rows of the X3's 99 B rows, 7,920 B, inside the kept block.
  assert(heapfloor::kGrayStripRows == 80 && heapfloor::kGrayStripRows * heapfloor::kWidestPanelRowBytes == 7920);
  assert(heapfloor::kGrayStripRows * heapfloor::kWidestPanelRowBytes <= heapfloor::kLargestFloorReported);
  std::puts("heap_floor: 7 checks passed");
#else
  std::puts("heap_floor: 5 checks passed");
#endif
  return 0;
}
