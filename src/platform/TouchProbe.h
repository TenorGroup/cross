#pragma once
// CMD:TAP and CMD:SWIPE of the measurement build: one planned finger, fed to the touch controller's
// read in place of the bus, so taps and swipes go through the same gesture code as a real finger.
// Pure arithmetic, checked on the host (test/touch_probe).
#include <cstdint>

namespace touchprobe {

struct Plan {
  uint32_t durationMs = 0;  // hold of a tap, travel time of a swipe
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool active = false;
  bool started = false;  // the first frame went out at startMs
  bool atEnd = false;    // a frame at (x1, y1) went out
  uint32_t startMs = 0;
};

inline Plan swipe(const int x0, const int y0, const int x1, const int y1, const uint32_t ms) {
  Plan plan;
  plan.durationMs = ms;
  plan.x0 = x0;
  plan.y0 = y0;
  plan.x1 = x1;
  plan.y1 = y1;
  plan.active = true;
  return plan;
}

inline Plan tap(const int x, const int y, const uint32_t holdMs) { return swipe(x, y, x, y, holdMs); }

// One controller frame at `now`: 1 = finger down at (x, y), 0 = finger lifted (the frame that ends the
// contact, sent once), -1 = nothing planned, read the real controller. The clock starts at the first
// frame, which is always the start point, and a late poll never skips the end point: the frame at
// (x1, y1) always goes out before the lift.
inline int8_t sample(Plan& plan, const uint32_t now, int& x, int& y) {
  if (!plan.active) return -1;
  if (!plan.started) {
    plan.started = true;
    plan.startMs = now;
  }
  const uint32_t elapsed = now - plan.startMs;
  if (elapsed < plan.durationMs) {
    const int64_t t = elapsed, d = plan.durationMs;
    x = plan.x0 + static_cast<int>((plan.x1 - plan.x0) * t / d);
    y = plan.y0 + static_cast<int>((plan.y1 - plan.y0) * t / d);
    return 1;
  }
  if (!plan.atEnd) {
    plan.atEnd = true;
    x = plan.x1;
    y = plan.y1;
    return 1;
  }
  plan.active = false;
  return 0;
}

// A point the app sees at (x, y) in `orientation` (GfxRenderer::Orientation order: Portrait,
// LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise), as the touch controller reports
// it: the inverse of GfxRenderer::tapToLogical after InputManager::normalizeTouchPoint. Each value
// is a touch unit in the middle of its panel pixel, so the round trip lands on the same pixel.
inline void toTouch(const int x, const int y, const int orientation, const int panelW, const int panelH,
                    const int touchW, const int touchH, uint16_t& tx, uint16_t& ty) {
  int px, py;
  switch (orientation) {
    case 0:
      px = y;
      py = panelH - 1 - x;
      break;
    case 1:
      px = panelW - 1 - x;
      py = panelH - 1 - y;
      break;
    case 2:
      px = panelW - 1 - y;
      py = x;
      break;
    default:
      px = x;
      py = y;
      break;
  }
  px = px < 0 ? 0 : (px >= panelW ? panelW - 1 : px);
  py = py < 0 ? 0 : (py >= panelH ? panelH - 1 : py);
  // Middle of the touch units that land on the pixel: from ceil(p * touch / panel) up to one below
  // ceil((p + 1) * touch / panel).
  const auto unit = [](const int p, const int panel, const int touch) {
    const int lo = (p * touch + panel - 1) / panel, hi = ((p + 1) * touch + panel - 1) / panel - 1;
    return static_cast<uint16_t>((lo + (hi > lo ? hi : lo)) / 2);
  };
  tx = unit(px, panelW, touchW);
  ty = unit(py, panelH, touchH);
}

}  // namespace touchprobe
