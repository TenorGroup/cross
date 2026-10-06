#pragma once
// tenor/ugly is a limited edition: v1.0.52 to v1.0.59, or two weeks from the day v1.0.52 is called stable,
// whichever comes first. The versions end it in the source (the first build past them drops the shell);
// the day ends it on the device, here. Pure, so a host test runs it.
#include <cstdint>

namespace shell::limit {

// The last day tenor/ugly is offered, YYYYMMDD in local time: set when v1.0.52 is called stable, two
// weeks on. 0 while there is no such day (the release candidates): limited, with no end date shown.
inline constexpr uint32_t UGLY_LAST_DAY = 20261020;

// Over once a last day is set and a clock that has been set (2026 or later; 0 is no clock) reads a
// later day.
inline bool over(const uint32_t lastDay, const uint32_t today) {
  return lastDay != 0 && today >= 20260101 && today > lastDay;
}

}  // namespace shell::limit
