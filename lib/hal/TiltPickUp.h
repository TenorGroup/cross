#pragma once

#include <cmath>
#include <cstdint>

// Tells a tilt flick from picking the device up. Both start with the same fast
// turn on the tilt axis, so a flick is held until the hand settles: a flick
// swings back, or comes to rest near the pose it started from; a pick-up leaves
// the device turned.
//
// The defaults come from an X3 held by hand; the recorded runs are in
// test/tilt_pick_up/x3-tilt-flicks-and-pick-ups.csv.
namespace TiltPickUp {

struct Thresholds {
  float returnDps = 150.0f;       // Swinging back this fast...
  unsigned long returnMs = 400;   // ...this soon after the trigger is a flick.
  float calmDps = 60.0f;          // Or: turning slower than this...
  int64_t poseCosSq64 = 55;       // ...with gravity within 22 degrees of the start: cos^2 >= 55/64.
  unsigned long maxWaitMs = 600;  // Neither by then: a pick-up.
};

enum class Verdict : uint8_t { Wait, Flick, PickUp };

// startMg: gravity before the flick, in mg. nowMg: the acceleration now.
// rateDps: the tilt axis now, signed so the flick's own direction is positive.
// ageMs: time since the flick triggered.
inline Verdict settle(const int32_t (&startMg)[3], const int32_t (&nowMg)[3], const float rateDps,
                      const unsigned long ageMs, const Thresholds& t = Thresholds{}) {
  if (ageMs <= t.returnMs && rateDps <= -t.returnDps) return Verdict::Flick;
  if (std::fabs(rateDps) < t.calmDps) {
    // 64-bit: the squares of the dot products overflow 32 bits.
    int64_t dot = 0, startSq = 0, nowSq = 0;
    for (int i = 0; i < 3; ++i) {
      dot += static_cast<int64_t>(startMg[i]) * nowMg[i];
      startSq += static_cast<int64_t>(startMg[i]) * startMg[i];
      nowSq += static_cast<int64_t>(nowMg[i]) * nowMg[i];
    }
    if (dot > 0 && 64 * dot * dot >= t.poseCosSq64 * startSq * nowSq) return Verdict::Flick;
  }
  return ageMs > t.maxWaitMs ? Verdict::PickUp : Verdict::Wait;
}

}  // namespace TiltPickUp
