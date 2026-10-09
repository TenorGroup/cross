#pragma once

#include <cstddef>
#include <cstdint>

namespace fontdownload {
inline bool hasTlsHeadroom(uint32_t freeHeap, uint32_t largestBlock, uint32_t minFree, uint32_t minLargest) {
  return freeHeap >= minFree && largestBlock >= minLargest;
}

inline bool canAllocateCatalog(uint64_t bytes, uint32_t freeHeap, uint32_t largestBlock) {
  return bytes <= freeHeap && uint64_t{freeHeap} - bytes >= 10240 &&
         bytes <= largestBlock && uint64_t{largestBlock} - bytes >= 8192;
}
}
