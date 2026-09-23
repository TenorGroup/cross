// The main loop can stall for seconds (a section build starved of heap while the
// radio starts). Every press the sample timer saw meanwhile must still reach the
// reader, one per frame, in the order the timer saw them.
#include <ButtonEdgeLatch.h>
#include <gtest/gtest.h>

namespace {
constexpr uint8_t kDown = 1u << 5;
constexpr uint8_t kUp = 1u << 4;

int framesWithPress(ButtonEdgeLatch& latch, const uint8_t bit, const int frames) {
  int count = 0;
  for (int i = 0; i < frames; ++i) {
    uint8_t pressed = 0, released = 0;
    latch.take(pressed, released);
    if (pressed & bit) ++count;
  }
  return count;
}
}  // namespace

TEST(ButtonEdgeLatch, FourTapsDuringOneStallBecomeFourPresses) {
  ButtonEdgeLatch latch;
  for (int i = 0; i < 4; ++i) {
    latch.add(kDown, 0);
    latch.add(0, kDown);
  }
  EXPECT_EQ(framesWithPress(latch, kDown, 6), 4);
}

TEST(ButtonEdgeLatch, OneTapIsOnePressAndOneRelease) {
  ButtonEdgeLatch latch;
  latch.add(kDown, 0);
  latch.add(0, kDown);
  uint8_t pressed = 0, released = 0;
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kDown);
  EXPECT_EQ(released, kDown);
  latch.take(pressed, released);
  EXPECT_EQ(pressed | released, 0);
}

TEST(ButtonEdgeLatch, ButtonsDrainIndependently) {
  ButtonEdgeLatch latch;
  latch.add(kDown, 0);
  latch.add(kDown | kUp, 0);
  uint8_t pressed = 0, released = 0;
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kDown | kUp);
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kDown);
  latch.take(pressed, released);
  EXPECT_EQ(pressed, 0);
}

TEST(ButtonEdgeLatch, AStuckSourceStaysBounded) {
  ButtonEdgeLatch latch;
  for (int i = 0; i < 1000; ++i) latch.add(kDown, 0);
  EXPECT_EQ(framesWithPress(latch, kDown, 300), ButtonEdgeLatch::kMaxPending);
}

namespace {
constexpr uint8_t kPowerIndex = ButtonFrames::kPowerButton;
constexpr uint8_t kPower = 1u << kPowerIndex;

ButtonSample sampleAt(const uint8_t state, const unsigned long heldMs, const unsigned long atMs) {
  ButtonSample sample;
  sample.state = state;
  sample.heldMs = heldMs;
  sample.powerHeldMs = heldMs;
  sample.atMs = atMs;
  return sample;
}
}  // namespace

// After a hold to wake, the main loop swallows the pass that no longer sees Power held, so
// the release of that hold is not taken as a short Power press (a page turn, a refresh,
// footnotes). The timer can see the release after a pass took its edges and before the
// pass asks whether Power is still held: the pass must answer from what it took, or it
// swallows itself without the release, and the release arrives on the next pass as a
// short press.
TEST(ButtonFrames, WakeReleaseSeenMidPassIsStillSwallowed) {
  ButtonFrames frames;
  frames.publish(0, 0, sampleAt(kPower, 900, 1000));  // the wake hold, past its press edge
  bool wakeReleasePending = true;
  int shortPowerActions = 0;
  // One main-loop pass in the order main.cpp runs it; `timerSeesRelease` is the sample
  // timer running while the pass is between taking its edges and the wake-release check.
  const auto pass = [&](const bool timerSeesRelease) {
    frames.beginFrame();
    if (timerSeesRelease) frames.publish(0, kPower, sampleAt(0, 910, 1010));
    if (wakeReleasePending && !frames.isPressed(kPowerIndex)) {
      wakeReleasePending = false;
      return;
    }
    if (frames.frameReleased & kPower) ++shortPowerActions;
  };
  pass(true);
  pass(false);
  pass(false);
  EXPECT_FALSE(wakeReleasePending);
  EXPECT_EQ(shortPowerActions, 0);
}

// Level, held time and edges of one pass all come from the same moment.
TEST(ButtonFrames, APassKeepsTheLevelAndHeldTimeItStartedWith) {
  constexpr uint8_t kDownIndex = 5;
  ButtonFrames frames;
  frames.publish(kDown, 0, sampleAt(kDown, 0, 1000));
  frames.publish(0, 0, sampleAt(kDown, 400, 1400));
  frames.beginFrame();
  EXPECT_EQ(frames.framePressed, kDown);
  frames.publish(0, kDown, sampleAt(0, 450, 1450));  // lifted after the pass began
  EXPECT_TRUE(frames.isPressed(kDownIndex));
  EXPECT_EQ(frames.heldTime(1420), 420u);
  EXPECT_EQ(frames.frameReleased, 0);
  frames.beginFrame();  // the next pass sees the release, level and edge together
  EXPECT_FALSE(frames.isPressed(kDownIndex));
  EXPECT_EQ(frames.frameReleased, kDown);
  EXPECT_EQ(frames.heldTime(1500), 450u);
}

// The idle poll still wakes on what the timer sees now, not on the last pass.
TEST(ButtonFrames, IdleWakeReadsTheLatestSample) {
  ButtonFrames frames;
  frames.beginFrame();
  EXPECT_FALSE(frames.active());
  ButtonSample weighing;
  weighing.debouncePending = true;
  frames.publish(0, 0, weighing);
  EXPECT_TRUE(frames.active());
}
