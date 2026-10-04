#pragma once

// Tap rect of the back button BaseTheme::drawHeader paints on touch boards.
// The header's FreeInkUI frame is non-interactive, so the rect is recorded
// here at draw time and consumed by MappedInputManager's Back mapping; every
// screen that draws a titled header gets tap-to-go-back without its own
// routing. Cleared on activity exit (and by headerless draws) so a stale rect
// never eats taps on a new screen. Written by the render task and read by the
// loop task; a torn int read at worst misroutes one tap on a band that is
// being redrawn, so no lock is taken.
namespace HeaderBackTapTarget {
inline int x = 0;
inline int y = 0;
inline int w = 0;
inline int h = 0;

inline void set(const int newX, const int newY, const int newW, const int newH) {
  x = newX;
  y = newY;
  w = newW;
  h = newH;
}

inline void clear() { w = 0; }

// Touch shell: the round "<" at the foot of a screen below another one is the same kind of target.
inline int footX = 0;
inline int footY = 0;
inline int footW = 0;
inline int footH = 0;
inline void setFoot(const int newX, const int newY, const int newW, const int newH) {
  footX = newX;
  footY = newY;
  footW = newW;
  footH = newH;
}
// The zone's round icon beside it (dynamic bar): a tap there leads to the zone's root, not Back.
inline int zoneX = 0;
inline int zoneY = 0;
inline int zoneW = 0;
inline int zoneH = 0;
inline void setZone(const int newX, const int newY, const int newW, const int newH) {
  zoneX = newX;
  zoneY = newY;
  zoneW = newW;
  zoneH = newH;
}
inline void clearFoot() {
  footW = 0;
  zoneW = 0;
}
inline bool zoneContains(const int tx, const int ty) {
  return zoneW > 0 && tx >= zoneX && tx < zoneX + zoneW && ty >= zoneY && ty < zoneY + zoneH;
}

// After a tap on "<", the next one is dropped for this long: a second tap meant for the screen that
// went is not a second Back on the screen that came (founder 04/10).
constexpr unsigned long FOOT_BACK_GUARD_MS = 600;
inline bool footContains(const int tx, const int ty) {
  return footW > 0 && tx >= footX && tx < footX + footW && ty >= footY && ty < footY + footH;
}
inline bool headerContains(const int tx, const int ty) { return w > 0 && tx >= x && tx < x + w && ty >= y && ty < y + h; }

inline bool contains(const int tx, const int ty) {
  return (w > 0 && tx >= x && tx < x + w && ty >= y && ty < y + h) ||
         (footW > 0 && tx >= footX && tx < footX + footW && ty >= footY && ty < footY + footH);
}
}  // namespace HeaderBackTapTarget
