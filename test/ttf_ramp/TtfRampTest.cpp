#include <ReaderInkWeight.h>
#include <gtest/gtest.h>

#include <map>

#include "FontCacheManager.h"
#include "FontDecompressor.h"
#include "SdCardFont.h"
#include "TtfEpdFont.h"

namespace {
const std::map<int, EpdFontFamily> kNoBuiltin;
const std::map<int, SdCardFont*> kNoSd;
}  // namespace

// The 6 public levels of a TTF reader font must reach the font with the same ink strengths the font
// packs and the TTF loader use (readerInk::outlineStrength): the manager passes the strength through.
TEST(TtfRamp, EachLevelUsesTheInkTable) {
  TtfEpdFont reader, other;
  const std::map<int, TtfEpdFont*> fonts{{7, &reader}, {8, &other}};
  FontCacheManager manager(kNoBuiltin, kNoSd, fonts);
  for (int level = 0; level < readerInk::LEVEL_COUNT; ++level) {
    manager.setReaderInk(7, readerInk::outlineStrength(level), true);
    EXPECT_EQ(reader.strength, readerInk::outlineStrength(level)) << "level " << level;
    EXPECT_EQ(other.strength, 0) << "level " << level << ": a font that is not the reader font stays flat";
  }
}

TEST(TtfRamp, AntiAliasingFlagAndUnknownLevelPassThrough) {
  TtfEpdFont reader;
  const std::map<int, TtfEpdFont*> fonts{{7, &reader}};
  FontCacheManager manager(kNoBuiltin, kNoSd, fonts);
  manager.setReaderInk(7, readerInk::outlineStrength(3), false);
  EXPECT_FALSE(reader.antiAliased);
  EXPECT_EQ(reader.strength, readerInk::outlineStrength(3));
  manager.setReaderInk(7, readerInk::outlineStrength(9), true);
  EXPECT_TRUE(reader.antiAliased);
  EXPECT_EQ(reader.strength, 0) << "a level outside 0-5 falls back to flat ink";
}
