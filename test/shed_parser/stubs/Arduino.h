#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>

inline unsigned long micros() { return 1; }
inline unsigned long millis() { return 1; }

// Heap probe. getFreeHeap() is what Section::stepHeapAvailable() asks at the top of every parse
// step, so answering "starved" on the k-th call puts the starvation at an exact step boundary.
struct EspHostStub {
  uint32_t maxAlloc = UINT32_MAX;
  uint32_t freeHeap = UINT32_MAX;
  uint64_t calls = 0;
  uint64_t starveAt = 0;     // starve on this call number (1-based), 0 = never
  uint64_t starveEvery = 0;  // starve on every Nth call, 0 = never
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
  uint32_t getFreeHeap() {
    ++calls;
    if ((starveAt && calls == starveAt) || (starveEvery && calls % starveEvery == 0)) return 1000;
    return freeHeap;
  }
  void restart() const { assert(false); }
};

inline EspHostStub ESP;
