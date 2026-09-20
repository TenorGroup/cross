#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>
inline uint32_t micros() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline uint32_t millis() { return micros() / 1000; }
