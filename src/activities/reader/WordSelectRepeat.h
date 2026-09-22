#pragma once

#include <cstdint>

// Auto-repeat of the Left/Right step on the quote selector. A quote of forty words used
// to cost forty presses, because the hold repeated at one fixed rate. The gap between
// steps now shrinks while the button stays down: a pause first, so a deliberate single
// step never runs away, then two faster rungs.
//
// Only this header decides when a held button owes another step; the screen asks it.
namespace wordselect {

// Hold this long before the first repeat, so a tap is never read as a hold.
constexpr unsigned long REPEAT_START_MS = 400;
// Second rung, entered after REPEAT_RUNG_STEPS repeats at the first one.
constexpr unsigned long REPEAT_FAST_MS = 150;
// Floor. Fast enough to cross a paragraph, slow enough that the panel keeps up.
constexpr unsigned long REPEAT_FASTEST_MS = 60;
constexpr int REPEAT_RUNG_STEPS = 3;

// Gap owed before the next step, given the repeats this hold has already made.
unsigned long delayMs(int stepsDone);

// Repeat state of one held direction button.
struct Repeat {
  // The press itself moved the cursor: the ramp starts from here.
  void pressed(unsigned long nowMs) {
    lastStepMs = nowMs;
    stepsDone = 0;
  }

  // The button came up (or no direction is down any more): the next hold pauses again.
  void released() { stepsDone = 0; }

  // True when the hold owes another step, and records it. The pause is measured from
  // the press, not from the input layer's held time, which counts any button.
  bool shouldStep(unsigned long nowMs);

  unsigned long lastStepMs = 0;
  int stepsDone = 0;
};

}  // namespace wordselect
