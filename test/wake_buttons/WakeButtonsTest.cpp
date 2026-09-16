#include <WakeButtons.h>
#include <gtest/gtest.h>

using namespace wakebuttons;

TEST(WakeButtons, EverySelectedButtonWorksAlone) {
  for (uint8_t mode = 0; mode < 4; ++mode) {
    for (uint8_t index = 0; index < 7; ++index) {
      HoldFilter filter;
      const uint8_t bit = 1u << index;
      EXPECT_FALSE(filter.sample(0, allowedMask(mode), 100));
      EXPECT_FALSE(filter.sample(bit, allowedMask(mode), 200));
      EXPECT_FALSE(filter.sample(bit, allowedMask(mode), 599));
      EXPECT_EQ(filter.sample(bit, allowedMask(mode), 600), (allowedMask(mode) & bit) != 0);
    }
  }
}

TEST(WakeButtons, SleepPressMustReleaseBeforeWake) {
  HoldFilter filter;
  EXPECT_FALSE(filter.sample(POWER, 127, 0));
  EXPECT_FALSE(filter.sample(POWER, 127, 5000));
  EXPECT_FALSE(filter.sample(0, 127, 5100));
  EXPECT_FALSE(filter.sample(POWER, 127, 5200));
  EXPECT_TRUE(filter.sample(POWER, 127, 5600));
}

TEST(WakeButtons, SwitchingKeysAndInterruptedHoldCannotAccumulate) {
  HoldFilter filter;
  filter.sample(0, 127, 0);
  filter.sample(LEFT_SIDE, 127, 100);
  EXPECT_FALSE(filter.sample(RIGHT_SIDE, 127, 400));
  EXPECT_FALSE(filter.sample(RIGHT_SIDE, 127, 700));
  EXPECT_FALSE(filter.sample(0, 127, 750));
  EXPECT_FALSE(filter.sample(RIGHT_SIDE, 127, 800));
  EXPECT_FALSE(filter.sample(RIGHT_SIDE, 127, 1100));
  EXPECT_TRUE(filter.sample(RIGHT_SIDE, 127, 1200));
}

TEST(WakeButtons, SameKeyCanStayHeldWhenAnotherKeyJoins) {
  HoldFilter filter;
  filter.sample(0, 127, 0);
  filter.sample(POWER, 127, 100);
  EXPECT_FALSE(filter.sample(POWER | LEFT_SIDE, 127, 300));
  EXPECT_TRUE(filter.sample(POWER | LEFT_SIDE, 127, 500));
}

TEST(WakeButtons, TimerWrapAndInvalidModeKeepPowerReachable) {
  HoldFilter filter;
  EXPECT_EQ(allowedMask(255), POWER);
  filter.sample(0, POWER, 0xfffffe00u);
  filter.sample(POWER, POWER, 0xffffff00u);
  EXPECT_FALSE(filter.sample(POWER, POWER, 143));
  EXPECT_TRUE(filter.sample(POWER, POWER, 144));
}

TEST(WakeButtons, StuckSideKeyCannotBlockPowerWake) {
  HoldFilter filter;
  EXPECT_FALSE(filter.sample(LEFT_SIDE, 127, 0));
  EXPECT_FALSE(filter.sample(LEFT_SIDE | POWER, 127, 100));
  EXPECT_TRUE(filter.sample(LEFT_SIDE | POWER, 127, 500));
}
