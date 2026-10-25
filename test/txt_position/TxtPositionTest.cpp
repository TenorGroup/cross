#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "TxtPosition.h"

namespace {

// The bytes v1.0.76's TxtReaderActivity::savePageIndexCache wrote (CACHE_VERSION 8): the same fields in the same
// order and widths, one writePod each.
template <typename T>
void pod(std::vector<uint8_t>& out, const T value) {
  uint8_t bytes[sizeof(T)];
  std::memcpy(bytes, &value, sizeof(T));
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

std::vector<uint8_t> index76(const uint32_t fileSize, const std::vector<uint32_t>& offsets, const uint8_t version = 8) {
  std::vector<uint8_t> out;
  pod<uint32_t>(out, 0x54585449);
  pod<uint8_t>(out, version);
  pod<uint32_t>(out, fileSize);
  pod<int32_t>(out, 480);  // viewportWidth
  pod<int32_t>(out, 33);   // linesPerPage
  pod<int32_t>(out, 1234); // fontId
  pod<int32_t>(out, 5);    // screen margin
  pod<uint8_t>(out, 0);    // paragraph alignment
  pod<int32_t>(out, 800);  // viewportHeight
  pod<int32_t>(out, 24);   // line height
  pod<int32_t>(out, 6);    // paragraph gap
  pod<int8_t>(out, -1);    // letter spacing
  pod<uint8_t>(out, 100);  // word spacing
  pod<uint16_t>(out, 2);   // indent
  pod<uint32_t>(out, static_cast<uint32_t>(offsets.size()));
  for (const uint32_t offset : offsets) pod<uint32_t>(out, offset);
  return out;
}

int percentAt(const std::vector<uint8_t>& index, const uint32_t sourceSize, const uint32_t page) {
  txtposition::IndexHeader header;
  if (!txtposition::parseIndexHeader(index.data(), index.size(), index.size(), sourceSize, header)) return -2;
  const uint32_t at = std::min(page, header.pageCount - 1);
  return txtposition::percentOfOffset(txtposition::le32(index.data() + txtposition::offsetAt(at)),
                                      header.sourceSize);
}

TEST(TxtPosition, HeaderSizeMatchesTheWrittenFields) {
  const auto index = index76(10000, {0});
  EXPECT_EQ(index.size(), txtposition::INDEX_HEADER_BYTES + 4);
}

TEST(TxtPosition, PageOffsetBecomesAShareOfTheFile) {
  const auto index = index76(10000, {0, 2000, 4000, 6500, 9000});
  EXPECT_EQ(percentAt(index, 10000, 0), 0);
  EXPECT_EQ(percentAt(index, 10000, 3), 65);
  EXPECT_EQ(percentAt(index, 10000, 4), 90);
}

TEST(TxtPosition, ProgressPastTheLastPageIsTheLastPage) {
  const auto index = index76(10000, {0, 2000, 9000});
  EXPECT_EQ(percentAt(index, 10000, 77), 90);
}

TEST(TxtPosition, ShareRoundsToWholePercent) {
  EXPECT_EQ(txtposition::percentOfOffset(1, 3), 33);
  EXPECT_EQ(txtposition::percentOfOffset(2, 3), 67);
  EXPECT_EQ(txtposition::percentOfOffset(3, 3), 100);
  EXPECT_EQ(txtposition::percentOfOffset(5, 0), -1);
  EXPECT_EQ(txtposition::percentOfOffset(4000000000u, 4100000000u), 98);
}

TEST(TxtPosition, ProgressFileHoldsTheLittleEndianPage) {
  const uint8_t data[4] = {0x34, 0x12, 0, 0};
  EXPECT_EQ(txtposition::pageOfProgress(data), 0x1234u);
}

TEST(TxtPosition, ForeignOrStaleIndexesAreRefused) {
  txtposition::IndexHeader header;
  auto wrongMagic = index76(10000, {0, 5000});
  wrongMagic[0] ^= 1;
  EXPECT_EQ(percentAt(wrongMagic, 10000, 1), -2);
  // Version 3 is the layout BookProgress.cpp of v1.0.76 expected; no TXT cache of that release has it.
  EXPECT_EQ(percentAt(index76(10000, {0, 5000}, 3), 10000, 1), -2);
  // The book on the card is not the one the index counted.
  EXPECT_EQ(percentAt(index76(10000, {0, 5000}), 10001, 1), -2);
  // A count that claims more offsets than the file holds.
  auto cut = index76(10000, {0, 5000, 7000});
  cut.resize(cut.size() - 4);
  EXPECT_FALSE(txtposition::parseIndexHeader(cut.data(), cut.size(), cut.size(), 10000, header));
  EXPECT_FALSE(txtposition::parseIndexHeader(cut.data(), 10, cut.size(), 10000, header));
  EXPECT_EQ(percentAt(index76(10000, {}), 10000, 0), -2);
}

TEST(TxtPosition, StatsFilePercentIsTheWholeKey) {
  auto percent = [](const std::string& text) { return txtposition::progressOfStatsJson(text.data(), text.size()); };
  EXPECT_EQ(percent(R"({"schema":4,"path":"/b.txt","progress":42,"startProgress":7})"), 42);
  EXPECT_EQ(percent(R"({"startProgress":7,"progress": 61})"), 61);
  EXPECT_EQ(percent(R"({"startProgress":7})"), -1);
  EXPECT_EQ(percent(R"({"progress":"x"})"), -1);
  EXPECT_EQ(percent(R"({"progress":250})"), 100);
  EXPECT_EQ(percent(""), -1);
}

TEST(TxtPosition, StatsFileNamedByTheFnvOfThePath) {
  EXPECT_EQ(txtposition::statsFileOf("/books/a.txt"), "/.crosspoint/reading-stats/tenor_3f5f93cce33fe762.json");
}

TEST(TxtPosition, TheIndexSaysFirstThenTheStats) {
  EXPECT_EQ(txtposition::percentToOpenAt(65, 40), 65);
  EXPECT_EQ(txtposition::percentToOpenAt(0, 40), 0);
  EXPECT_EQ(txtposition::percentToOpenAt(-1, 40), 40);
  EXPECT_EQ(txtposition::percentToOpenAt(-1, -1), -1);
}

TEST(TxtPosition, TheOldCacheGoesOnlyWithAPlaceThatWasLaidOut) {
  EXPECT_TRUE(txtposition::legacyCacheMayGo(true, false, true));
  EXPECT_FALSE(txtposition::legacyCacheMayGo(false, false, true));  // the card refused the save
  EXPECT_FALSE(txtposition::legacyCacheMayGo(true, true, true));    // the jump to the old share is still waiting
  EXPECT_FALSE(txtposition::legacyCacheMayGo(true, true, false));   // ... with no section yet
  EXPECT_FALSE(txtposition::legacyCacheMayGo(true, false, false));  // no page stands (the heap could not build one)
}

}  // namespace
