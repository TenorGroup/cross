// Dynamic bar rule 11: a flick turns a page, a slow drag moves the rows the finger travelled.
#include <gtest/gtest.h>

#include "components/TouchScroll.h"

using touchscroll::rows;

TEST(TouchScroll, FlickTurnsAPage) {
  EXPECT_EQ(rows(-400, 150, 62, 10), 10);
  EXPECT_EQ(rows(400, 150, 62, 10), -10);
}

TEST(TouchScroll, SlowDragMovesTheRowsTravelled) {
  EXPECT_EQ(rows(-250, 1200, 62, 10), 4);  // 250 px is 4 rows of 62
  EXPECT_EQ(rows(150, 1200, 62, 10), -2);
  EXPECT_EQ(rows(-60, 1200, 62, 10), 1);   // the shortest drag still moves a row
}

TEST(TouchScroll, NoTravelNoMove) { EXPECT_EQ(rows(0, 1200, 62, 10), 0); }
