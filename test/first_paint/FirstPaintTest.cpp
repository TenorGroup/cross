// The X4 Pro opens a book from a menu with the fast refresh; a wake into the book and the X3 keep the
// cleaning pass.
#include <gtest/gtest.h>

#include "activities/reader/FirstPaint.h"

TEST(FirstPaint, X4ProFromMenuIsFast) { EXPECT_TRUE(fastFirstPaint(true, ReaderOpen::FromMenu)); }

TEST(FirstPaint, X4ProWakeCleans) { EXPECT_FALSE(fastFirstPaint(true, ReaderOpen::Other)); }

TEST(FirstPaint, X3Cleans) {
  EXPECT_FALSE(fastFirstPaint(false, ReaderOpen::FromMenu));
  EXPECT_FALSE(fastFirstPaint(false, ReaderOpen::Other));
}
