#pragma once
#include <cstdint>
unsigned long millis();
inline unsigned long fixedWaitMs = 0;
inline void delay(unsigned long ms) { fixedWaitMs += ms; }
struct FakeEsp {
  uint32_t getFreeHeap() const { return 100000; }
  uint32_t getMaxAllocHeap() const { return 60000; }
};
inline FakeEsp ESP;
