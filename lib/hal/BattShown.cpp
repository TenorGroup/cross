#include "BattShown.h"

namespace battshown {

namespace {
constexpr uint32_t STEP_INTERVAL_MS = 60000;  // one point at most per this many ms while discharging
constexpr uint8_t EMPTY_THRESHOLD = 2;        // raw at or below this skips the rate limit entirely
}  // namespace

uint8_t next(State& s, uint8_t raw, const bool charging, const uint32_t nowMs) {
  if (raw > 100) raw = 100;

  if (!s.hasValue) {
    s.hasValue = true;
    s.shown = raw;
    s.lastStepMs = nowMs;
    return s.shown;
  }

  if (charging) {
    if (raw > s.shown) s.shown = raw;
    // Keep the cooldown fresh so an unplug starts its own 60s wait instead of
    // inheriting a timestamp from whenever discharging last stepped.
    s.lastStepMs = nowMs;
    return s.shown;
  }

  // Discharging: shown never rises, so only a smaller raw can move it.
  if (raw < s.shown) {
    if (raw <= EMPTY_THRESHOLD) {
      // The gauge says the battery is empty: warn immediately, no cooldown.
      s.shown = raw;
      s.lastStepMs = nowMs;
    } else if (nowMs - s.lastStepMs >= STEP_INTERVAL_MS) {
      --s.shown;
      s.lastStepMs = nowMs;
    }
  }
  return s.shown;
}

}  // namespace battshown
