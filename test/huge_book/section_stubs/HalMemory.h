#pragma once
// Device heap as the huge-book tests model it: whatever the test cap leaves.
#include <HeapCapState.h>

#include <cstddef>
class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes;
    size_t totalBytes;
    size_t minFreeBytes;
    size_t largestBlockBytes;
  };
  static HeapStats getDefaultHeap() {
    const size_t left = heapcap::available();
    return {left, left, left, left};
  }
};
