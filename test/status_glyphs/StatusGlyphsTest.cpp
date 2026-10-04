// Dynamic bar rule 10: the remote's glyph in the status strip.
#include <gtest/gtest.h>

#include "components/StatusGlyphs.h"

using statusglyph::Bt;
using statusglyph::bluetooth;

TEST(StatusGlyphs, RadioOffShowsNothing) { EXPECT_EQ(bluetooth(false, false, false, 0), Bt::None); }

TEST(StatusGlyphs, LinkingThenLinked) {
  EXPECT_EQ(bluetooth(true, false, false, 1000), Bt::Linking);
  EXPECT_EQ(bluetooth(true, true, false, 0), Bt::Linked);
}

TEST(StatusGlyphs, DroppedOrNeverLinkedIsLost) {
  EXPECT_EQ(bluetooth(true, false, true, 0), Bt::Lost);
  EXPECT_EQ(bluetooth(true, false, false, statusglyph::BT_LINK_GIVE_UP_MS + 1), Bt::Lost);
}
