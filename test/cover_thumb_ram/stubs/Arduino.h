#pragma once
#include <Print.h>

#include <cstddef>
#include <cstdint>
// The heap a test sets: GrayThumb::start() checks it before taking its buffers.
// `taken` counts what the test's allocations took since it was reset, as the device heap would.
struct EspClass {
  uint32_t freeHeap = 200000;
  uint32_t taken = 0;
  uint32_t getFreeHeap() { return freeHeap > taken ? freeHeap - taken : 0; }
};
extern EspClass ESP;
