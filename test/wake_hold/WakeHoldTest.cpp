// The X3 reads a power-key wake as a real one only after the key has stayed down for 400 ms. The
// watch lets the boot run meanwhile: a timer feeds it samples, the boot asks early (nothing
// waits) and once more at the end, after the window.
#include <WakeHold.h>
#include <gtest/gtest.h>

namespace {
constexpr uint16_t kWindowMs = 400;
}

TEST(WakeHold, ReleasedBeforeTheFirstSampleFailsEarly) {
  wakehold::Watch watch;
  watch.start(false, 0, kWindowMs);
  EXPECT_FALSE(watch.early());
  EXPECT_FALSE(watch.final(430));
}

TEST(WakeHold, ReleasedAt200IsStillHeldAt100ButFailsTheEnd) {
  wakehold::Watch watch;
  watch.start(true, 0, kWindowMs);
  for (uint32_t t = 5; t < 200; t += 5) watch.sample(true, t);
  EXPECT_TRUE(watch.early());  // asked at 100 ms: nothing seen yet
  watch.sample(false, 200);    // let go at 200 ms
  EXPECT_FALSE(watch.early());
  EXPECT_FALSE(watch.final(430));
}

TEST(WakeHold, HeldThroughTheWindowPasses) {
  wakehold::Watch watch;
  watch.start(true, 0, kWindowMs);
  for (uint32_t t = 5; t <= 450; t += 5) watch.sample(true, t);
  EXPECT_TRUE(watch.early());
  EXPECT_TRUE(watch.final(429));
}

TEST(WakeHold, RemainingMsCountsDownFromTheStart) {
  wakehold::Watch watch;
  watch.start(true, 1000, kWindowMs);
  EXPECT_EQ(watch.remainingMs(1000), 400u);
  EXPECT_EQ(watch.remainingMs(1300), 100u);
  EXPECT_EQ(watch.remainingMs(1400), 0u);
  EXPECT_EQ(watch.remainingMs(1900), 0u);
}

TEST(WakeHold, TheEndWaitsOutTheWindow) {
  wakehold::Watch watch;
  watch.start(true, 0, kWindowMs);
  watch.sample(true, 5);
  EXPECT_FALSE(watch.final(300));  // 100 ms still to wait: not an answer yet
  EXPECT_TRUE(watch.final(400));
}

TEST(WakeHold, ARelease20MsLongIsNeverTakenBack) {
  wakehold::Watch watch;
  watch.start(true, 0, kWindowMs);
  watch.sample(true, 5);
  watch.sample(false, 10);  // 20 ms off ...
  watch.sample(true, 30);   // ... and down again before anyone asked
  EXPECT_FALSE(watch.early());
  EXPECT_FALSE(watch.final(430));
}

TEST(WakeHold, AReleaseAfterTheWindowIsNotAFailure) {
  // The boot asks for the answer well after the window (here at 549 ms): a key let go at 450 ms,
  // past the 400 ms window, is a hold that held.
  wakehold::Watch watch;
  watch.start(true, 0, kWindowMs);
  for (uint32_t t = 5; t < 400; t += 5) watch.sample(true, t);
  watch.sample(false, 450);
  EXPECT_TRUE(watch.early());
  EXPECT_TRUE(watch.final(549));
}

TEST(WakeHold, AReleaseJustInsideTheWindowIsAFailure) {
  wakehold::Watch watch;
  watch.start(true, 0, kWindowMs);
  watch.sample(false, 399);
  EXPECT_FALSE(watch.final(549));
}

TEST(WakeHold, TheClockMayWrap) {
  wakehold::Watch watch;
  watch.start(true, 0xFFFFFF00u, kWindowMs);
  EXPECT_EQ(watch.remainingMs(0x10u), 400u - 0x110u);
  watch.sample(true, 0x10u);
  watch.sample(false, 0x120u);  // past the window, across the wrap
  EXPECT_TRUE(watch.final(0x130u));
}
