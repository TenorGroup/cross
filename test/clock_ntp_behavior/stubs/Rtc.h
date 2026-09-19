#pragma once
#include <cstdint>
class Rtc {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0, weekday = 0;
  };
  bool begin();
  bool now(DateTime&);
  bool set(const DateTime&);
};
