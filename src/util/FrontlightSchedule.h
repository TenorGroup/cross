#pragma once

#include <cstdint>

// The light's daily hours (Settings > Sleep, X4 Pro): a wake or a start inside [from, until) turns the light on.
// The window may run past midnight (21 to 6). Equal hours mean no schedule. Applied at boot only: the
// light is never switched while the reader is awake.
namespace frontlightschedule {

inline bool lightOnAt(const uint8_t hour, const uint8_t from, const uint8_t until) {
  if (from == until) return false;
  return from < until ? hour >= from && hour < until : hour >= from || hour < until;
}

}  // namespace frontlightschedule
