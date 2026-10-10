#pragma once

#include <HeapFloor.h>

#include <cstddef>
#include <cstdint>

namespace fontdownload {
inline bool hasTlsHeadroom(uint32_t freeHeap, uint32_t largestBlock, uint32_t minFree, uint32_t minLargest) {
  return freeHeap >= minFree && largestBlock >= minLargest;
}

// The catalog fits when the heap left after it stays on the heap floor (heapfloor::crosses).
inline bool canAllocateCatalog(uint64_t bytes, uint32_t freeHeap, uint32_t largestBlock) {
  return bytes <= freeHeap && bytes <= largestBlock && !heapfloor::crosses(freeHeap - bytes, largestBlock - bytes);
}
}
