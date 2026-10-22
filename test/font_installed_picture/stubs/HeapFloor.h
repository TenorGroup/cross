#pragma once

// Host stub: the real door's header, with a switch that makes the door refuse a block from 4 KB up as the device's
// does when it would leave the heap under the floor (host builds have no heap to keep, so judge() always stands).
#define ESP_PLATFORM 1
#include "../../../lib/Memory/HeapFloor.h"
#undef ESP_PLATFORM
namespace heapfloor {
inline bool refuseBig = false;
inline void failed(size_t, const char*) {}
inline bool judge(size_t bytes, const void*, const char*) {
  if (!refuseBig || bytes < kBigBytes) return true;
  refusals.fetch_add(1);
  return false;
}
inline void* take(size_t bytes, const char* who) {
  void* block = malloc(bytes);
  if (block && !judge(bytes, block, who)) {
    free(block);
    return nullptr;
  }
  return block;
}
}  // namespace heapfloor
