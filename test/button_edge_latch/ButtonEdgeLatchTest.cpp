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
