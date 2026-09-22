#pragma once

#include <cstdint>

inline int64_t esp_timer_get_time() {
  static int64_t now = 1000;
  return now += 10;
}
