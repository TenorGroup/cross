#include <gtest/gtest.h>

#include "platform/HangWatch.h"

using hangwatch::kLimitMs;
using hangwatch::kLoop;
using hangwatch::kRender;
using hangwatch::stuck;

TEST(HangWatch, ALoopPassWithinTheLimitIsAlive) {
  EXPECT_EQ(stuck(100000, 100000 - (kLimitMs - 1), 0), 0);
  EXPECT_EQ(stuck(100000, 100000 - kLimitMs, 0), kLoop);
}

TEST(HangWatch, ARenderTaskWaitingForWorkIsNeverStuck) {
  // 0: no paint in progress, however long ago the last one ended.
  EXPECT_EQ(stuck(4000000000u, 4000000000u, 0), 0);
}

TEST(HangWatch, APaintPastTheLimitIsStuckOnItsOwnOrWithTheLoop) {
  EXPECT_EQ(stuck(100000, 99990, 100000 - kLimitMs), kRender);
  EXPECT_EQ(stuck(100000, 100000 - kLimitMs, 100000 - kLimitMs), kLoop | kRender);
  EXPECT_EQ(stuck(100000, 99990, 100000 - (kLimitMs - 1)), 0);
}

TEST(HangWatch, MillisWrappingIsNotAHang) {
  // The loop ran 5 s before millis() wrapped, the paint began 1 s before: 6 s and 2 s ago.
  EXPECT_EQ(stuck(1000, 0xFFFFFFFFu - 4999, 0xFFFFFFFFu - 999), 0);
  EXPECT_EQ(stuck(kLimitMs, 0xFFFFFFFFu, 0), kLoop);
}
