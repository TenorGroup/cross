#pragma once
#include <cstddef>
#include <cstdint>
class HalClock {
 public:
  bool formatTime(char*, size_t, bool) const { return false; }
  bool getDateTime(uint16_t&, uint8_t&, uint8_t&, uint8_t&, uint8_t&) const { return false; }
};
extern HalClock halClock;
