#include "WordSelectRepeat.h"

namespace wordselect {

unsigned long delayMs(const int stepsDone) {
  if (stepsDone <= 0) return REPEAT_START_MS;
  if (stepsDone < REPEAT_RUNG_STEPS) return REPEAT_FAST_MS;
  return REPEAT_FASTEST_MS;
}

bool Repeat::shouldStep(const unsigned long nowMs) {
  if (nowMs - lastStepMs < delayMs(stepsDone)) return false;
  lastStepMs = nowMs;
  // Stop counting at the last rung: the delay never changes again, and the counter must
  // not run away over a hold the user forgot about.
  if (stepsDone < REPEAT_RUNG_STEPS) stepsDone++;
  return true;
}

}  // namespace wordselect
