#pragma once

#include <cstdint>

// The light's daily window (Settings > Sleep, X4 Pro): a wake or a start inside [from, until) turns the light on.
// The window may run past midnight (21 to 6). Equal ends mean no schedule. Applied at boot only: the
// light is never switched while the reader is awake. Settings keeps each end as an hour (as v1.0.76 did) and a
// minute in steps of 5, so a file with hours only reads as the window it was.
namespace frontlightschedule {

inline uint16_t minuteOfDay(const uint8_t hour, const uint8_t minute) { return static_cast<uint16_t>(hour * 60 + minute); }

// Minutes of the day, 0..1439.
inline bool lightOnAtMinute(const uint16_t now, const uint16_t from, const uint16_t until) {
  if (from == until) return false;
  return from < until ? now >= from && now < until : now >= from || now < until;
}

inline bool lightOnAt(const uint8_t hour, const uint8_t from, const uint8_t until) {
  return lightOnAtMinute(minuteOfDay(hour, 0), minuteOfDay(from, 0), minuteOfDay(until, 0));
}

}  // namespace frontlightschedule
