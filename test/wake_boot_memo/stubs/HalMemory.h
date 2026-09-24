#pragma once
#include <cstddef>
class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes = 0;
    size_t largestBlockBytes = 0;
    size_t minFreeBytes = 0;
  };
  static HeapStats getInternalHeap() { return {}; }
};
