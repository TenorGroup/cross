#pragma once
#include <cstdint>
#include <ctime>
// No clock unless a test sets one: `date` (UTC, offset 0) for the calendar, `local` for the hour of day.
struct FakeClock {
  struct Date {
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0;
  };
  Date date{};
  bool dateSet = false;
  struct tm local {};
  bool localSet = false;
  bool getDateTime(uint16_t& y, uint8_t& mo, uint8_t& d, uint8_t& h, uint8_t& mi) {
    if (!dateSet) return false;
    y = date.year, mo = date.month, d = date.day, h = date.hour, mi = date.minute;
    return true;
  }
  bool utcOffsetMinutes(int& minutes) const {
    minutes = 0;
    return dateSet;
  }
  bool localTime(struct tm& out) const { out = local; return localSet; }
};
inline FakeClock halClock;
