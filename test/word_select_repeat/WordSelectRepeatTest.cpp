// Holding Left/Right on the quote selector must walk a long quote without dozens of
// presses: the gap between steps shrinks while the button stays down. The screen owns no
// timing of its own, so these are the real numbers the device uses.
#include <activities/reader/WordSelectRepeat.h>
#include <gtest/gtest.h>

namespace {

// Drive one uninterrupted hold on a 10 ms loop tick and count the steps it produces.
int stepsWhileHolding(const unsigned long holdMs) {
  wordselect::Repeat repeat;
  repeat.pressed(0);
  int steps = 0;
  for (unsigned long now = 10; now <= holdMs; now += 10) {
    if (repeat.shouldStep(now)) steps++;
  }
  return steps;
}

TEST(WordSelectRepeat, TheRampGetsFasterAndThenSettles) {
  EXPECT_EQ(wordselect::delayMs(0), wordselect::REPEAT_START_MS);
  EXPECT_EQ(wordselect::delayMs(1), wordselect::REPEAT_FAST_MS);
  EXPECT_EQ(wordselect::delayMs(wordselect::REPEAT_RUNG_STEPS), wordselect::REPEAT_FASTEST_MS);
  EXPECT_EQ(wordselect::delayMs(40), wordselect::REPEAT_FASTEST_MS);
}

// A tap that is shorter than the pause must not repeat at all: one press, one word.
TEST(WordSelectRepeat, AShortTapNeverRepeats) { EXPECT_EQ(stepsWhileHolding(wordselect::REPEAT_START_MS - 10), 0); }

TEST(WordSelectRepeat, TheFirstRepeatWaitsForThePause) {
  wordselect::Repeat repeat;
  repeat.pressed(0);
  EXPECT_FALSE(repeat.shouldStep(wordselect::REPEAT_START_MS - 1));
  EXPECT_TRUE(repeat.shouldStep(wordselect::REPEAT_START_MS));
}

// The point of the change: a forty-word quote has to be reachable with one hold of a
// couple of seconds, where the old fixed 500 ms rate gave three steps in that time.
TEST(WordSelectRepeat, TwoSecondsOfHoldingCrossesALongQuote) {
  EXPECT_GE(stepsWhileHolding(2000), 20);
  EXPECT_LE(stepsWhileHolding(2000), 30);
}

TEST(WordSelectRepeat, ReleasingRestartsThePause) {
  wordselect::Repeat repeat;
  repeat.pressed(0);
  ASSERT_TRUE(repeat.shouldStep(wordselect::REPEAT_START_MS));
  repeat.released();
  repeat.pressed(1000);
  EXPECT_FALSE(repeat.shouldStep(1000 + wordselect::REPEAT_START_MS - 1));
  EXPECT_TRUE(repeat.shouldStep(1000 + wordselect::REPEAT_START_MS));
}

}  // namespace
