#pragma once
#include <Print.h>

#include <cstddef>
#include <cstdint>
struct EspClass {
  uint32_t getFreeHeap() { return 200000; }
};
extern EspClass ESP;
inline unsigned long millis() { return 0; }
inline void vTaskDelay(int) {}
