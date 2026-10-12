#pragma once
#include <cstdint>
#include <ctime>
// No calendar date; a test sets `local` (and `localSet`) for the hour of day.
struct FakeClock {
  struct tm local {};
  bool localSet = false;
  bool getDateTime(uint16_t&, uint8_t&, uint8_t&, uint8_t&, uint8_t&){return false;}
  bool utcOffsetMinutes(int&) const {return false;}
  bool localTime(struct tm& out) const { out = local; return localSet; }
};
inline FakeClock halClock;
