#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
inline unsigned long micros() { return 1; }
inline unsigned long millis() { return 1; }
struct EspClass {
  uint32_t getFreeHeap() const { return 2000000; }
  uint32_t getMaxAllocHeap() const { return 1000000; }
  void restart() const { assert(false); }
};
inline EspClass ESP;
