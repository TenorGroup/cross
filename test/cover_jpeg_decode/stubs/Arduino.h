#pragma once
#include <Print.h>

#include <cstddef>
#include <cstdint>
// The heap a test sets: the decoders and GrayThumb::start() check it before taking buffers.
struct EspClass {
  uint32_t freeHeap = 200000;
  uint32_t getFreeHeap() { return freeHeap; }
};
extern EspClass ESP;
inline unsigned long millis() { return 0; }
