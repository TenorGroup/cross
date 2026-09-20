#pragma once

#include <cstdint>

struct EspHostStub {
  uint32_t freeHeap = UINT32_MAX;
  mutable uint32_t freeHeapQueries = 0;

  uint32_t getFreeHeap() const {
    ++freeHeapQueries;
    return freeHeap;
  }

  void reset() {
    freeHeap = UINT32_MAX;
    freeHeapQueries = 0;
  }
};

inline EspHostStub ESP;
