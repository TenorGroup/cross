#pragma once
#include <Print.h>

#include <cstddef>
#include <cstdint>
// The heap a test sets: GrayThumb::start() checks it before taking its buffers.
struct EspClass {
  uint32_t freeHeap = 200000;
  uint32_t getFreeHeap() { return freeHeap; }
};
extern EspClass ESP;
