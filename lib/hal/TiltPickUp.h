#pragma once

#include <cmath>
#include <cstdint>

// Tells a tilt flick from picking the device up. Both start with the same fast
// turn on the tilt axis, so a flick is held until the device is at rest again:
// back near the pose it started from after a flick, turned away after a pick-up.
// At rest means the tilt axis has stopped and the accelerometer reads gravity
// alone. While a hand lifts the device, the lift adds to gravity and can hold
// the reading near the starting pose, and the device can swing back on the way.
//
// The defaults come from an X3 held by hand; clips of the recorded runs replay
// in test/tilt_pick_up/TiltPickUpTest.cpp.
namespace TiltPickUp {

struct Thresholds {
  float calmDps = 60.0f;          // At rest: turning slower than this,
  int32_t restMg = 100;           // the acceleration within this of 1 g,
  int64_t poseCosSq64 = 55;       // and gravity within 22 degrees of the start: cos^2 >= 55/64.
  unsigned long maxWaitMs = 800;  // Not back at rest by then: a pick-up.
};

enum class Verdict : uint8_t { Wait, Flick, PickUp };

// startMg: gravity before the flick, in mg. nowMg: the acceleration now.
// rateDps: the tilt axis now. ageMs: time since the flick triggered.
inline Verdict settle(const int32_t (&startMg)[3], const int32_t (&nowMg)[3], const float rateDps,
                      const unsigned long ageMs, const Thresholds& t = Thresholds{}) {
  // Past the wait, even a return to the start is dropped.
  if (ageMs > t.maxWaitMs) return Verdict::PickUp;
  if (std::fabs(rateDps) < t.calmDps) {
    // 64-bit: the squares of the dot products overflow 32 bits.
    int64_t dot = 0, startSq = 0, nowSq = 0;
    for (int i = 0; i < 3; ++i) {
      dot += static_cast<int64_t>(startMg[i]) * nowMg[i];
      startSq += static_cast<int64_t>(startMg[i]) * startMg[i];
      nowSq += static_cast<int64_t>(nowMg[i]) * nowMg[i];
    }
    const int64_t low = 1000 - t.restMg;
    const int64_t high = 1000 + t.restMg;
    const bool gravityOnly = nowSq >= low * low && nowSq <= high * high;
    if (gravityOnly && dot > 0 && 64 * dot * dot >= t.poseCosSq64 * startSq * nowSq) return Verdict::Flick;
  }
  return Verdict::Wait;
}

}  // namespace TiltPickUp
