#include <gtest/gtest.h>

#include "src/ReaderTextRelayout.h"

namespace {
// The reader loop: a press touches the timer, every tick asks whether the quiet time has passed and
// lays the chapter out once when it has. Returns how many layouts a run of presses caused.
int layoutsFor(const unsigned long* pressAt, const int presses, const unsigned long endMs) {
  TextRelayoutQuiet quiet;
  int layouts = 0, next = 0;
  for (unsigned long now = 0; now <= endMs; now += 10) {
    while (next < presses && pressAt[next] <= now) {
      quiet.touch(pressAt[next]);
      ++next;
    }
    if (quiet.due(now)) {
      ++layouts;
      quiet.clear();
    }
  }
  return layouts;
}
}  // namespace

TEST(TextRelayoutQuietTest, FourQuickPressesAreOneLayout) {
  const unsigned long presses[] = {0, 200, 400, 600};
  EXPECT_EQ(layoutsFor(presses, 4, 5000), 1);
}

TEST(TextRelayoutQuietTest, NothingLaidOutBetweenPresses) {
  TextRelayoutQuiet quiet;
  quiet.touch(1000);
  EXPECT_FALSE(quiet.due(1000));
  EXPECT_FALSE(quiet.due(1599));
  quiet.touch(1500);  // the next press moves the deadline
  EXPECT_FALSE(quiet.due(2099));
  EXPECT_TRUE(quiet.due(2100));
}

TEST(TextRelayoutQuietTest, SlowPressesAreOneLayoutEach) {
  const unsigned long presses[] = {0, 1000, 2000};
  EXPECT_EQ(layoutsFor(presses, 3, 6000), 3);
}

TEST(TextRelayoutQuietTest, ClearedAndIdleNeverDue) {
  TextRelayoutQuiet quiet;
  EXPECT_FALSE(quiet.due(100000));
  quiet.touch(0);
  quiet.clear();
  EXPECT_FALSE(quiet.isPending());
  EXPECT_FALSE(quiet.due(100000));
}

TEST(TextRelayoutQuietTest, SurvivesTheMillisWrap) {
  TextRelayoutQuiet quiet;
  const unsigned long nearWrap = static_cast<unsigned long>(-200L);
  quiet.touch(nearWrap);
  EXPECT_FALSE(quiet.due(nearWrap + 100));
  EXPECT_TRUE(quiet.due(nearWrap + TextRelayoutQuiet::kQuietMs));  // past the wrap
}
