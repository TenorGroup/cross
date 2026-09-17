#pragma once

#include <cstddef>

class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes;
    size_t totalBytes;
    size_t minFreeBytes;
    size_t largestBlockBytes;
  };

  static HeapStats getInternalHeap() {
    ++internalHeapReads;
    return internalHeap;
  }

  static inline HeapStats internalHeap{0, 0, 0, 0};
  static inline unsigned internalHeapReads = 0;
};
