#pragma once

#include <cstdint>

// The battery percentage the UI shows, smoothed on top of the raw gauge/ADC reading.
//
// The BQ27220 fuel gauge corrects its remaining-capacity estimate at three fixed
// voltage thresholds (TRM SLUUBD4A section 1.1.1) and again whenever it re-measures
// open-circuit voltage, both by design. A domestic X3 unit showed this as 26% then a
// straight drop to 0%, and while charging 7% after one minute, then 55%, then 72%.
// This module does not change the
// gauge or the raw value anything else reads; it only smooths the number a screen
// draws, so safety logic (low-battery sleep, charging decisions) must keep reading
// the raw percentage instead.
namespace battshown {

struct State {
  bool hasValue = false;  // false until the first sample (also true again after a reboot/wake,
                           // since this struct is a fresh global each boot)
  uint8_t shown = 0;
  uint32_t lastStepMs = 0;  // last time `shown` moved down one point while discharging
};

// Advances `s` with one new raw sample and returns the percentage to show.
//
// - First sample (s.hasValue == false): shown = raw.
// - Charging: shown never drops; it rises to raw immediately.
// - Discharging: shown never rises. When raw < shown, shown drops by at most one
//   point every 60,000 ms of nowMs, stepping toward raw. raw <= 2 (the gauge says
//   the battery is empty) skips the rate limit entirely so the low-battery warning
//   is never held back by it.
// - Switching between charging and discharging keeps the current `shown` as the
//   start; nothing is reset except the 60,000 ms cooldown, which restarts on every
//   charging call so an unplug does not inherit a stale timestamp and step down
//   right away.
uint8_t next(State& s, uint8_t raw, bool charging, uint32_t nowMs);

}  // namespace battshown
