#pragma once
#include <HalMemory.h>
#include <cstring>
#define ESP_PLATFORM 1
#include "../../../lib/Memory/HeapFloor.h"
#undef ESP_PLATFORM
namespace heapfloor {
inline bool refuseRuby = false;
inline bool judge(size_t bytes, const void*, const char* who) {
  if (refuseRuby && strncmp(who, "ruby-", 5) == 0) return false;
  const auto heap = HalMemory::getDefaultHeap();
  return stands(bytes, heap.freeBytes, heap.largestBlockBytes);
}
inline void* take(size_t bytes, const char* who) {
  void* block = malloc(bytes);
  if (block && !judge(bytes, block, who)) { free(block); return nullptr; }
  return block;
}
}
