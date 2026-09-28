#pragma once
#include <cstdint>
unsigned long millis();
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(millis()) * 1000; }
