#include <gtest/gtest.h>

#include "util/FrontlightSchedule.h"

using frontlightschedule::lightOnAt;

TEST(FrontlightSchedule, EqualHoursMeanNoSchedule) {
  for (uint8_t h = 0; h < 24; ++h) EXPECT_FALSE(lightOnAt(h, 7, 7));
}

TEST(FrontlightSchedule, ADayWindowIncludesItsStartAndNotItsEnd) {
  EXPECT_FALSE(lightOnAt(17, 18, 23));
  EXPECT_TRUE(lightOnAt(18, 18, 23));
  EXPECT_TRUE(lightOnAt(22, 18, 23));
  EXPECT_FALSE(lightOnAt(23, 18, 23));
}

TEST(FrontlightSchedule, AWindowRunsPastMidnight) {
  EXPECT_TRUE(lightOnAt(21, 21, 6));
  EXPECT_TRUE(lightOnAt(0, 21, 6));
  EXPECT_TRUE(lightOnAt(5, 21, 6));
  EXPECT_FALSE(lightOnAt(6, 21, 6));
  EXPECT_FALSE(lightOnAt(12, 21, 6));
}
