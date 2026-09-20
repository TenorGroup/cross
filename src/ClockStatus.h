#pragma once

#include <HalClock.h>
#include <cstdint>

namespace clockstatus {
// A clock setting stays reachable before sync. Displayed time depends on the
// current epoch, including boards whose clock comes from NTP alone.
inline bool hasValidTime() {
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0;
  return halClock.getDateTime(year, month, day, hour, minute) && year >= 2025 && year <= 2099;
}
}  // namespace clockstatus
