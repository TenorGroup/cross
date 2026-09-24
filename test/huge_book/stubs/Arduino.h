#pragma once
#include <HeapCapState.h>

#include <cstdint>

// Free heap as the device reports it: whatever the test cap leaves.
struct EspHostStub {
  uint32_t getFreeHeap() const { return clamp(heapcap::available()); }
  uint32_t getMaxAllocHeap() const { return clamp(heapcap::available()); }
  static uint32_t clamp(size_t v) { return v > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(v); }
};
inline EspHostStub ESP;
inline unsigned long millis() { return 0; }
