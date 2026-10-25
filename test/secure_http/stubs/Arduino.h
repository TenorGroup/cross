#pragma once
#include <cstddef>
#include <cstdint>
inline unsigned long millis() {
  static unsigned long value = 0;
  return ++value;
}
inline void delay(unsigned long) {}
// The heap as the firmware reports it; a case lowers it to see what a login does on the heap a radio leaves.
struct EspHostStub {
  uint32_t freeHeap = UINT32_MAX;
  uint32_t maxAlloc = UINT32_MAX;
  uint32_t getFreeHeap() const { return freeHeap; }
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
};
inline EspHostStub ESP;
