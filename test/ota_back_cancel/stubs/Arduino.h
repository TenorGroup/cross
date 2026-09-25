#pragma once
#include <cstdint>
unsigned long millis();
inline void delay(unsigned long) {}
struct FakeEsp {
  uint32_t getFreeHeap() const { return 100000; }
  uint32_t getMaxAllocHeap() const { return 60000; }
};
inline FakeEsp ESP;
