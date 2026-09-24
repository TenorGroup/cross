// The main loop can stall for seconds (a section build starved of heap while the
// radio starts). Every press the sample timer saw meanwhile must still reach the
// reader, one per frame, in the order the timer saw them, across buttons too.
#include <ButtonEdgeLatch.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

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

// Edges leave in the order they came: a second press of Down waits for the next frame, and the
// Up pressed with it waits with it instead of jumping ahead.
TEST(ButtonEdgeLatch, EdgesLeaveInTheOrderTheyCame) {
  ButtonEdgeLatch latch;
  latch.add(kDown, 0);
  latch.add(kDown | kUp, 0);
  uint8_t pressed = 0, released = 0;
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kDown);
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kDown | kUp);
  latch.take(pressed, released);
  EXPECT_EQ(pressed, 0);
}

// A tap of one button, then a tap of another: one frame each, first tap first.
TEST(ButtonEdgeLatch, TapsOfTwoButtonsKeepTheirOrder) {
  ButtonEdgeLatch latch;
  latch.add(kDown, 0);
  latch.add(0, kDown);
  latch.add(kUp, 0);
  latch.add(0, kUp);
  uint8_t pressed = 0, released = 0;
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kDown);
  EXPECT_EQ(released, kDown);
  latch.take(pressed, released);
  EXPECT_EQ(pressed, kUp);
  EXPECT_EQ(released, kUp);
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

// A stall with Next tapped and then Previous: the reader turns one page on each frame's release
// and checks Previous first, so both releases in one frame would add up to one page back instead
// of forward and back again.
TEST(ButtonFrames, NextThenPreviousDuringOneStallTurnsForwardThenBack) {
  ButtonFrames frames;
  frames.publish(kDown, 0, sampleAt(kDown, 0, 1000));
  frames.publish(0, kDown, sampleAt(0, 80, 1080));
  frames.publish(kUp, 0, sampleAt(kUp, 0, 1200));
  frames.publish(0, kUp, sampleAt(0, 90, 1290));
  int page = 0;
  int lowest = 0;
  for (int i = 0; i < 4; ++i) {
    frames.beginFrame();
    if (frames.frameReleased & kUp)
      --page;
    else if (frames.frameReleased & kDown)
      ++page;
    lowest = std::min(lowest, page);
  }
  EXPECT_EQ(page, 0);
  EXPECT_EQ(lowest, 0);  // forward first, as pressed
}

// A short tap and then a long hold, both during one stall: the tap's release, handed out frames
// later, still reads the tap's own held time, so it is not taken for a long press.
TEST(ButtonFrames, AQueuedReleaseKeepsItsOwnHeldTime) {
  constexpr uint8_t kBack = 1u << 0;
  ButtonFrames frames;
  frames.publish(kPower, 0, sampleAt(kPower, 0, 1000));
  frames.publish(0, kPower, sampleAt(0, 80, 1080));
  frames.publish(kBack, 0, sampleAt(kBack, 0, 1200));
  frames.publish(0, 0, sampleAt(kBack, 800, 2000));
  frames.beginFrame();
  EXPECT_EQ(frames.frameReleased, kPower);
  EXPECT_EQ(frames.heldTime(2000), 80u);
  EXPECT_EQ(frames.powerHeldTime(2000), 80u);
  frames.beginFrame();
  EXPECT_EQ(frames.framePressed, kBack);
  EXPECT_EQ(frames.heldTime(2000), 800u);
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

// The timer is the only reader of the ladder once `sampleTimer` is set: update() then serves the
// frames it publishes and never runs the debounce itself. A timer created but never started would
// leave every button dead, so the handle is kept only once the start succeeded, and a failed start
// deletes the timer and stays on the polled path.
TEST(HalGpioSampling, TheTimerIsKeptOnlyOnceItRuns) {
  std::ifstream file(REPO_ROOT_PATH "/lib/hal/HalGPIO.cpp");
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const size_t begin = text.find("void HalGPIO::startBackgroundSampling()");
  ASSERT_NE(begin, std::string::npos);
  const std::string body = text.substr(begin, text.find("\n}\n", begin) - begin);
  const size_t start = body.find("esp_timer_start_periodic(timer, 10000) != ESP_OK");
  ASSERT_NE(start, std::string::npos) << "the start's result is not checked";
  EXPECT_NE(body.find("esp_timer_delete(timer)", start), std::string::npos) << "a failed start leaks its timer";
  EXPECT_GT(body.find("sampleTimer = timer"), start) << "the handle is kept before the timer runs";
}
