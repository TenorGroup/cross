#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

class GfxRenderer;

// Two fingers on the X4 Pro set the light while they move: up/down brightness, right/left warmth, in 5%
// steps. Main task only; the common frame hook draws the level it shows.
struct FrontlightGesture {
  // What the frame shows.
  bool vertical = true;
  uint8_t value = 0;
  uint32_t changedAt = 0;
  bool visible = false;
  bool dirty = false;
  // A fast flick down put the light out; this is the brightness it keeps for the next step up.
  uint8_t keep = 0;

  static constexpr uint32_t IDLE_MS = 1500;
  // 5% per 30 px of the fingers' centre: the 6 px per 1% the release-time rule had, in founder's steps.
  static constexpr int STEP_PX = 30;
  static constexpr int STEP_PERCENT = 5;
  // A two-finger flick down puts the light out whatever its level: at least 120 px (14 mm at 8.62 px/mm)
  // at 0.6 px/ms or faster, so 120 px in 200 ms. A list flick turns a page within 300 ms over 60 px or more
  // (0.2 px/ms, touchscroll); a hand stepping the light down travels 30 px per step and stops to read the
  // number, well under 0.6. Estimated from those thresholds, not yet from GT911 traces: TENOR_PRESS_PROBE
  // logs every flick's speed ([LGT]) to set this on the device.
  static constexpr int FLICK_MIN_PX = 120;
  static constexpr int FLICK_MIN_PX_PER_S = 600;

  bool expired(uint32_t now) const { return visible && now - changedAt >= IDLE_MS; }

  // The fingers' centre at (x, y), screen pixels, with two contacts down. True when `value` is new or the
  // level first shows: the caller sets it (vertical: brightness, 0 = off; else warmth) and repaints.
  bool follow(const int x, const int y, const uint8_t brightness, const uint8_t warmth, const bool on,
              const uint32_t now) {
    if (!tracking) {
      tracking = seen = true;
      locked = false;
      startX = x;
      startY = y;
      startBrightness = brightness;
      startWarmth = warmth;
      startOn = on;
      return false;
    }
    const int dx = x - startX;
    const int dy = startY - y;  // up is positive
    if (!locked) {
      const int ax = std::abs(dx), ay = std::abs(dy);
      if (std::max(ax, ay) < STEP_PX) return false;
      if (2 * ay >= 3 * ax)
        vertical = true;
      else if (2 * ax >= 3 * ay)
        vertical = false;
      else
        return false;
      locked = true;
      shown = -1;
    }
    const int steps = (vertical ? dy : dx) / STEP_PX;
    int level;
    if (!vertical)
      level = startWarmth + steps * STEP_PERCENT;
    else if (startOn)
      level = startBrightness + steps * STEP_PERCENT;
    else  // from off: the first step up brings back the kept level (5% when none was kept)
      level = steps <= 0 ? 0 : (startBrightness ? startBrightness : STEP_PERCENT) + (steps - 1) * STEP_PERCENT;
    level = std::clamp(level, 0, 100);
    if (level == shown) return false;
    shown = level;
    value = static_cast<uint8_t>(level);
    visible = true;
    changedAt = now;
    return true;
  }
  // The fingers are no longer two on the glass.
  void lift() { tracking = false; }
  bool following() const { return tracking; }

  // The SDK's classified two-finger swipe, on release: dx, dy in screen pixels (down positive). True for a
  // fast flick down: the caller keeps `keep` as the brightness and puts the light out.
  bool flick(const int dx, const int dy, const uint32_t durationMs, const uint8_t brightness, const uint32_t now) {
    if (dy < FLICK_MIN_PX || 2 * dy < 3 * std::abs(dx) || dy * 1000L < FLICK_MIN_PX_PER_S * long(durationMs))
      return false;
    keep = seen && startOn ? startBrightness : brightness;
    seen = false;
    vertical = true;
    value = 0;
    visible = true;
    changedAt = now;
    return true;
  }

  void draw(const GfxRenderer& r, int barTop) const;

 private:
  bool tracking = false;
  bool seen = false;  // a follow() began since the last flick
  bool locked = false;
  int shown = -1;
  int startX = 0;
  int startY = 0;
  uint8_t startBrightness = 0;
  uint8_t startWarmth = 0;
  bool startOn = false;
};
