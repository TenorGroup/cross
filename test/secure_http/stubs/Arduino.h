#pragma once
#include <cstddef>
#include <cstdint>
inline unsigned long millis() {
  static unsigned long value = 0;
  return ++value;
}
inline void delay(unsigned long) {}
// The heap as the firmware reports it; a case lowers it to see what a login does on the heap a radio leaves.
// A suite with a heap of its own (secure_client) defines HOST_STUB_OWN_ESP before it includes this.
#ifndef HOST_STUB_OWN_ESP
struct EspHostStub {
  uint32_t freeHeap = UINT32_MAX;
  uint32_t maxAlloc = UINT32_MAX;
  uint32_t getFreeHeap() const { return freeHeap; }
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
};
inline EspHostStub ESP;
#endif
