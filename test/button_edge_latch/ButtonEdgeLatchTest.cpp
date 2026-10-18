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

// The X3 probe records 20 short taps while one display refresh holds the main
// loop. Every press and release must survive that stall for the keyboard to
// receive all 20 actions.
TEST(ButtonEdgeLatch, TwentyTapsDuringOneStallAreNotLost) {
  ButtonEdgeLatch latch;
  for (int i = 0; i < 20; ++i) {
    latch.add(kDown, 0);
    latch.add(0, kDown);
  }
  EXPECT_EQ(framesWithPress(latch, kDown, 24), 20);
  EXPECT_FALSE(latch.pending());
  EXPECT_FALSE(latch.overflowed());
  EXPECT_EQ(latch.droppedCount(), 0u);
}

TEST(ButtonEdgeLatch, QueueAndAccountingStayBounded) {
  EXPECT_LT(sizeof(ButtonEdgeLatch), 2048u);
}

TEST(ButtonEdgeLatch, OverflowIsCountedAndNewEdgesWaitForSafeDrain) {
  ButtonEdgeLatch latch;
  for (int i = 0; i < ButtonEdgeLatch::kMaxPending; ++i) latch.add(kDown, 0);
  latch.add(kUp, 0);
  latch.add(kDown, 0);

  EXPECT_TRUE(latch.overflowed());
  EXPECT_EQ(latch.droppedCount(), 2u);
  EXPECT_TRUE(latch.pending());

  uint8_t pressed = 0, released = 0;
  latch.take(pressed, released);
  EXPECT_TRUE(latch.overflowed());
  latch.add(kUp, 0);
  EXPECT_EQ(latch.droppedCount(), 3u);

  while (latch.take(pressed, released)) {
  }
  EXPECT_FALSE(latch.pending());
  EXPECT_FALSE(latch.overflowed());

  latch.add(kUp, 0);
  EXPECT_TRUE(latch.take(pressed, released));
  EXPECT_EQ(pressed, kUp);
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

TEST(ButtonFrames, OverflowSuppressesActionsUntilTheLatchDrains) {
  ButtonFrames frames;
  for (int i = 0; i < ButtonEdgeLatch::kMaxPending; ++i) frames.publish(kDown, 0, sampleAt(kDown, 0, 1000 + i));
  frames.publish(kUp, 0, sampleAt(kUp, 0, 2000));
  frames.beginFrame();
  EXPECT_EQ(frames.framePressed, 0);
  EXPECT_TRUE(frames.edges.overflowed());
  while (frames.edges.pending()) frames.beginFrame();
  EXPECT_FALSE(frames.edges.overflowed());
  frames.publish(kUp, 0, sampleAt(kUp, 0, 3000));
  frames.beginFrame();
  EXPECT_EQ(frames.framePressed, kUp);
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

// X4 Classic and X4 Pro read plain GPIO keys; the main loop waits on every display frame, so a
// quick tap in that wait was never seen (5 of 20 taps typing in the Wi-Fi keyboard on both). Every
// board with ADC or GPIO keys hands them to the sample timer, a touch panel or not.
TEST(ButtonSampling, EveryBoardWithKeysUsesTheTimer) {
  EXPECT_TRUE(samplesInBackground(true, false));    // X3, X4: ADC ladder
  EXPECT_TRUE(samplesInBackground(false, true));    // X4 Classic, X4 Pro (touch panel too): GPIO keys
  EXPECT_FALSE(samplesInBackground(false, false));  // any other input style
}

// The start decision goes through that rule. The touch panel shares its I2C bus with the fuel gauge
// (X4 Pro), so its reads stay on the main loop: on a board with a touch profile or a controller that
// came up, the timer runs a reader of its own that never started the panel, and the loop keeps
// polling the panel through inputMgr.
namespace {
std::string halGpioBody(const char* signature) {
  std::ifstream file(REPO_ROOT_PATH "/lib/hal/HalGPIO.cpp");
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const size_t begin = text.find(signature);
  if (begin == std::string::npos) return {};
  return text.substr(begin, text.find("\n}\n", begin) - begin);
}
}  // namespace

TEST(HalGpioSampling, TheStartFollowsTheRuleAndKeepsTouchOffTheTimer) {
  const std::string body = halGpioBody("void HalGPIO::startBackgroundSampling()");
  ASSERT_FALSE(body.empty());
  EXPECT_NE(body.find("samplesInBackground("), std::string::npos) << "the start does not use the rule";
  const size_t touch = body.find("ACTIVE.touch.controller != BoardConfig::TouchController::None");
  EXPECT_NE(touch, std::string::npos) << "the touch panel is judged only by whether its init worked";
  EXPECT_NE(body.find("inputMgr.hasTouch()"), std::string::npos);
  const size_t ownReader = body.find("new (std::nothrow) InputManager()");
  ASSERT_NE(ownReader, std::string::npos) << "a touch board's timer would run inputMgr and its touch I2C";
  EXPECT_GT(ownReader, touch);
  EXPECT_EQ(body.find("begin()"), std::string::npos) << "the timer's reader must never start the panel";
}

// The own reader sits behind the SDK's touch capability. A misspelt macro still compiles (an
// unknown name in #if reads as 0) and would put inputMgr, GT911 included, on the timer's core.
TEST(HalGpioSampling, TheOwnReaderIsBuiltUnderTheTouchCapability) {
  const std::string body = halGpioBody("void HalGPIO::startBackgroundSampling()");
  const size_t ownReader = body.find("new (std::nothrow) InputManager()");
  ASSERT_NE(ownReader, std::string::npos);
  const size_t guard = body.rfind("\n#if", ownReader);
  ASSERT_NE(guard, std::string::npos) << "the own reader is not behind a capability";
  EXPECT_EQ(body.substr(guard + 1, body.find('\n', guard + 1) - guard - 1), "#if FREEINK_CAP_TOUCH");
  EXPECT_GT(body.find("\n#endif", ownReader), ownReader);

  std::ifstream file(REPO_ROOT_PATH "/freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h");
  const std::string sdk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const size_t define = sdk.find("#define FREEINK_CAP_TOUCH ");
  ASSERT_NE(define, std::string::npos) << "the SDK no longer defines FREEINK_CAP_TOUCH";
  const size_t x4pro = sdk.find("FREEINK_DEVICE_X4PRO", define);
  EXPECT_LT(x4pro, sdk.find("#endif", define)) << "FREEINK_CAP_TOUCH is not set on the X4 Pro";
}

// A key held while the timer starts (the power key held to wake) must read as held, not as a fresh
// press: the own reader settles its debounce on the keys as they are before the first tick.
TEST(HalGpioSampling, TheOwnReaderSettlesOnHeldKeysBeforeTheTimer) {
  const std::string body = halGpioBody("void HalGPIO::startBackgroundSampling()");
  const size_t ownReader = body.find("new (std::nothrow) InputManager()");
  const size_t publish = body.find("sampledInput = input;");
  ASSERT_NE(ownReader, std::string::npos);
  ASSERT_NE(publish, std::string::npos);
  const size_t settle = body.find("input->isDebouncePending()", ownReader);
  EXPECT_LT(settle, publish) << "the own reader starts unsettled: a held key becomes a press";
  EXPECT_LT(body.find("input->update();", settle), publish) << "the settling loop never samples";
}

// The timer's first tick runs on the other core and dereferences sampledInput at once.
TEST(HalGpioSampling, TheReaderIsPublishedBeforeTheTimerStarts) {
  const std::string body = halGpioBody("void HalGPIO::startBackgroundSampling()");
  const size_t start = body.find("esp_timer_start_periodic(");
  ASSERT_NE(start, std::string::npos);
  EXPECT_LT(body.find("sampledInput = input;"), start) << "the first tick reads a null reader";
}

TEST(HalGpioSampling, TheTimerReadsItsReaderAndTheLoopKeepsThePanel) {
  const std::string sample = halGpioBody("void HalGPIO::sampleButtons(void* self)");
  ASSERT_FALSE(sample.empty());
  EXPECT_NE(sample.find("->sampledInput"), std::string::npos);
  EXPECT_EQ(sample.find("->inputMgr"), std::string::npos) << "the timer reads inputMgr, touch included";
  const std::string update = halGpioBody("void HalGPIO::update()");
  const size_t frame = update.find("frames.beginFrame()");
  ASSERT_NE(frame, std::string::npos);
  const size_t panel = update.find("if (sampledInput != &inputMgr) inputMgr.update();", frame);
  EXPECT_NE(panel, std::string::npos) << "with the timer on, nothing polls the touch panel";
}
