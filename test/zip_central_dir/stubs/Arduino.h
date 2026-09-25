#pragma once
#include <cstddef>
#include <cstdint>
// The heap a test sets: the central directory walk sizes its block by it.
struct EspClass {
  uint32_t freeHeap = 200000;
  uint32_t getFreeHeap() { return freeHeap; }
};
extern EspClass ESP;
inline unsigned long millis() { return 0; }
