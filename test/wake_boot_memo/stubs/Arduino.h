#pragma once
#include <cstdint>
#define RTC_NOINIT_ATTR
inline void delay(uint32_t) {}
inline unsigned long millis() { return 0; }
