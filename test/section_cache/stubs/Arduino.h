#pragma once

#include <cstdint>

struct EspHostStub {
  uint32_t maxAlloc = UINT32_MAX;
  uint32_t freeHeap = UINT32_MAX;
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
  uint32_t getFreeHeap() const { return freeHeap; }
};

inline EspHostStub ESP;
