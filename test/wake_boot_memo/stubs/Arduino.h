#pragma once
#include <cstddef>
#include <cstdint>
#define RTC_NOINIT_ATTR
inline void delay(uint32_t) {}
inline unsigned long millis() { return 0; }
// A roomy heap: the variant loads are not refused for want of a free block.
struct HostEsp {
  size_t getMaxAllocHeap() const { return 256 * 1024; }
  size_t getFreeHeap() const { return 256 * 1024; }
};
inline HostEsp ESP;
