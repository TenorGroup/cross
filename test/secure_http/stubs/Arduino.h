#pragma once
#include <cstddef>
#include <cstdint>
inline unsigned long millis() {
  static unsigned long value = 0;
  return ++value;
}
inline void delay(unsigned long) {}
