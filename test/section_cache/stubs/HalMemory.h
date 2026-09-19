#pragma once
#include <cstddef>
#include <limits>
class HalMemory {
 public:
  struct HeapStats { size_t freeBytes; size_t totalBytes; size_t minFreeBytes; size_t largestBlockBytes; };
  inline static size_t largestBlockBytes = std::numeric_limits<size_t>::max();
  static HeapStats getDefaultHeap() { return {largestBlockBytes, largestBlockBytes, largestBlockBytes, largestBlockBytes}; }
};
