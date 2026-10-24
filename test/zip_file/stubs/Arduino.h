#pragma once
#include <cstddef>
#include <cstdint>
// The heap a test sets: the central directory walk and the heap floor size their blocks by it.
struct EspClass {
  uint32_t freeHeap = 200000;
  uint32_t getMaxAllocHeap() { return freeHeap; }
  uint32_t getFreeHeap() { return freeHeap; }
};
inline EspClass ESP;
inline unsigned long millis() { return 0; }
