// tenor/ugly: what the shell decides without a panel, and the font it draws with.
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "shells/ugly/UglyLogic.h"
#include "shells/ugly/UglyTables.h"
#include "shells/ugly/fonts/ugly_22.h"
#include "shells/ugly/fonts/ugly_30.h"
#include "shells/ugly/fonts/ugly_38.h"
#include "shells/ugly/fonts/ugly_52.h"

namespace {
using namespace ugly::logic;

TEST(Jump, SameStringSamePixels) {
  for (int px : {22, 30, 38, 52})
    for (int pos = 0; pos < 40; ++pos)
      for (uint32_t cp : {0x61u, 0x1EA1u, 0x20u}) {
        EXPECT_EQ(jumpDy(pos, cp, px), jumpDy(pos, cp, px));
        EXPECT_LE(std::abs(jumpDy(pos, cp, px)), 2 * px / 38 + 1);
        EXPECT_LE(std::abs(jumpStep(pos, cp)), 1);
      }
}

TEST(Jump, LettersDoNotAllRideTheSameLine) {
  std::set<int> seen;
  for (int pos = 0; pos < 5; ++pos) seen.insert(jumpDy(pos, 'a', 38));
  EXPECT_GE(seen.size(), 4u) << "five neighbours must land on several different heights";
}

TEST(Wobble, StaysInRangeAndRepeats) {
  for (uint32_t seed : {5u, 13u, 501u})
    for (int i = 0; i < 50; ++i) {
      EXPECT_LE(std::abs(wobble(seed, i, 2)), 2);
      EXPECT_EQ(wobble(seed, i, 2), wobble(seed, i, 2));
    }
}

TEST(Days, KnownDates) {
  EXPECT_EQ(civilDays(19700101), 0);
  EXPECT_EQ(civilDays(20000301), 11017);
  EXPECT_EQ(daysBetween(20260930, 20261004), 4);
  EXPECT_EQ(daysBetween(20260228, 20260301), 1) << "2026 is not a leap year";
  EXPECT_EQ(daysBetween(20240228, 20240301), 2) << "2024 is";
  EXPECT_EQ(daysBetween(20251231, 20260101), 1);
}

TEST(Diary, SentenceByLastDay) {
  int days = -1;
  EXPECT_EQ(diaryKind(false, 20261003, 20261004), DiaryKind::NoBook);
  EXPECT_EQ(diaryKind(true, 0, 20261004), DiaryKind::NoStats);
  EXPECT_EQ(diaryKind(true, 20261004, 20261004), DiaryKind::Today);
  EXPECT_EQ(diaryKind(true, 20261003, 20261004), DiaryKind::Yesterday);
  EXPECT_EQ(diaryKind(true, 20260930, 20261004, &days), DiaryKind::Ago);
  EXPECT_EQ(days, 4);
  EXPECT_EQ(diaryKind(true, 20260101, 20261004), DiaryKind::Before) << "past 60 days the count is not said";
  EXPECT_EQ(diaryKind(true, 20260930, 0), DiaryKind::Before) << "no clock, no count";
}

TEST(Sleep, OneLinePerDayAlwaysInRange) {
  std::set<int> seen;
  for (uint32_t day = 20261001; day <= 20261031; ++day) {
    const int a = sleepLine(day, 5);
    EXPECT_EQ(a, sleepLine(day, 5));
    EXPECT_GE(a, 0);
    EXPECT_LT(a, 5);
    seen.insert(a);
  }
  EXPECT_EQ(seen.size(), 5u) << "a month shows every line";
  EXPECT_EQ(sleepLine(0, 5), 0);
}

TEST(Layout, BreaksOnWidthAndNeverInsideAToken) {
  const std::vector<std::string> words = {"Hom", "qua", "may", "doc", "Kid napped", "toi"};
  std::vector<Token> t;
  for (auto& w : words) t.push_back({w.c_str(), 0, false});
  t.push_back({"?", 0, true});
  std::vector<Placed> out(t.size());
  const auto measure = [](const char* s) { return static_cast<int>(std::string(s).size()) * 10; };
  const int lines = layout(t.data(), static_cast<int>(t.size()), 100, 10, measure, out.data());
  // "Hom qua may" is 30+10+30+10+30 = 110 > 100, so the third word starts line 1.
  EXPECT_EQ(out[0].line, 0);
  EXPECT_EQ(out[1].line, 0);
  EXPECT_EQ(out[2].line, 1);
  EXPECT_EQ(out[4].w, 100) << "a token wider than a line sits alone, whole";
  EXPECT_EQ(out[6].line, out[5].line) << "an attached token stays with the one before";
  EXPECT_EQ(out[6].x, out[5].x + out[5].w) << "and takes no blank";
  EXPECT_EQ(lines, out[6].line + 1);
}

TEST(Layout, BlankLineBetweenParagraphs) {
  Token t[] = {{"a", 0, false}, {nullptr, 0, false}, {nullptr, 0, false}, {"b", 0, false}};
  Placed out[4];
  const int lines = layout(t, 4, 100, 10, [](const char*) { return 10; }, out);
  EXPECT_EQ(out[0].line, 0);
  EXPECT_EQ(out[3].line, 2);
  EXPECT_EQ(lines, 3);
}

TEST(Pages, RowsOfANotebookPage) {
  EXPECT_EQ(pageTop(0, 9), 0);
  EXPECT_EQ(pageTop(8, 9), 0);
  EXPECT_EQ(pageTop(9, 9), 9);
  EXPECT_EQ(pageCount(0, 9), 1);
  EXPECT_EQ(pageCount(9, 9), 1);
  EXPECT_EQ(pageCount(10, 9), 2);
  EXPECT_EQ(cycle(0, -1, 5), 4);
  EXPECT_EQ(cycle(4, 1, 5), 0);
  EXPECT_EQ(cycle(0, 1, 0), 0);
}

TEST(FolderCap, FollowsTheHeapAndNeverPassesTheCeiling) {
  EXPECT_EQ(folderCap(0, 0), 0u);
  EXPECT_EQ(folderCap(FOLDER_HEAP_KEEP, 1 << 20), 0u) << "nothing left after what the screen keeps";
  EXPECT_EQ(folderCap(1 << 20, 1 << 20), FOLDER_MAX_ROWS) << "a roomy heap still stops at the ceiling";
  EXPECT_EQ(folderCap(30000, 1 << 20), 170u);
  // BLE on: about 45 KB free in blocks of 24 KB, which is still room for a few hundred names
  EXPECT_GT(folderCap(45 * 1024, 24 * 1024), 300u);
  // a block too small for the vector to double in bounds the list whatever the total
  EXPECT_EQ(folderCap(1 << 20, 4800), 4800 / (2 * sizeof(std::string)));
  size_t last = 0;
  for (size_t free = 0; free < 400 * 1024; free += 997) {
    const size_t cap = folderCap(free, free);
    EXPECT_GE(cap, last) << "more heap never means a lower ceiling";
    EXPECT_LE(cap * FOLDER_BYTES_PER_ROW + FOLDER_HEAP_KEEP, std::max(free, FOLDER_HEAP_KEEP));
    last = cap;
  }
}

TEST(Circles, EndsCrossAndStayNearTheBox) {
  using namespace ugly;
  for (const auto* pts : {CIRCLE_WORD, CIRCLE_OBJECT, CIRCLE_ROW}) {
    const int n = pts == CIRCLE_WORD ? CIRCLE_WORD_COUNT : pts == CIRCLE_OBJECT ? CIRCLE_OBJECT_COUNT : CIRCLE_ROW_COUNT;
    for (int i = 0; i < n; ++i) {
      EXPECT_LE(std::abs(pts[i].x), 300);
      EXPECT_LE(std::abs(pts[i].y), 300);
    }
    // One loop with the pen going on past its start: the two ends sit apart, on the same side.
    EXPECT_GT(std::abs(pts[0].x - pts[n - 1].x) + std::abs(pts[0].y - pts[n - 1].y), 0);
  }
}

// The baked fonts: every Vietnamese letter has ink at every size that carries text.
void checkFont(const EpdFontData& f, const std::vector<uint32_t>& wanted, const char* name) {
  std::set<uint32_t> have;
  uint32_t expectedOffset = 0;
  for (uint32_t i = 0; i < f.intervalCount; ++i) {
    const auto& r = f.intervals[i];
    EXPECT_LE(r.first, r.last) << name;
    if (i > 0) EXPECT_GT(r.first, f.intervals[i - 1].last) << name << " intervals sorted and apart";
    for (uint32_t cp = r.first; cp <= r.last; ++cp) {
      have.insert(cp);
      const auto& g = f.glyph[r.offset + cp - r.first];
      EXPECT_EQ(g.dataOffset, expectedOffset) << name << " U+" << std::hex << cp;
      expectedOffset += g.dataLength;
      EXPECT_EQ(g.dataLength, (g.width * g.height + 7) / 8) << name << " U+" << std::hex << cp;
      if (cp != 0x20) EXPECT_GT(g.width * g.height, 0) << name << " U+" << std::hex << cp << " has no ink";
      EXPECT_GT(g.advanceX, 0) << name;
    }
  }
  for (uint32_t cp : wanted) EXPECT_TRUE(have.count(cp)) << name << " lacks U+" << std::hex << cp;
}

TEST(Font, VietnameseAndAsciiAtBodySizes) {
  std::vector<uint32_t> wanted;
  for (uint32_t cp = 0x20; cp < 0x7F; ++cp) wanted.push_back(cp);
  for (uint32_t cp = 0x1EA0; cp <= 0x1EF9; ++cp) wanted.push_back(cp);
  for (uint32_t cp : {0xC0u, 0xC1u, 0xC2u, 0xC3u, 0xC8u, 0xC9u, 0xCAu, 0xCCu, 0xCDu, 0xD2u, 0xD3u, 0xD4u, 0xD5u, 0xD9u, 0xDAu, 0xDDu,
                      0xE0u, 0xE1u, 0xE2u, 0xE3u, 0xE8u, 0xE9u, 0xEAu, 0xECu, 0xEDu, 0xF2u, 0xF3u, 0xF4u, 0xF5u, 0xF9u, 0xFAu, 0xFDu,
                      0x102u, 0x103u, 0x110u, 0x111u, 0x128u, 0x129u, 0x168u, 0x169u, 0x1A0u, 0x1A1u, 0x1AFu, 0x1B0u})
    wanted.push_back(cp);
  checkFont(ugly_22, wanted, "ugly_22");
  checkFont(ugly_30, wanted, "ugly_30");
  checkFont(ugly_38, wanted, "ugly_38");
}

TEST(Font, TitleSizeHasTheLettersOfThePageNames) {
  std::vector<uint32_t> wanted;
  for (const char* c : {"G", "ầ", "n", "đ", "â", "y", "T", "h", "ư", "m", "ụ", "c", "Y", "ê", "u", "í", "ố", "g", "k", "ê", "C", "à", "i", "ặ", "t", "F", "e", "S", "s", "/", "0", "9"}) {
    // decode one UTF-8 character
    const unsigned char* p = reinterpret_cast<const unsigned char*>(c);
    uint32_t cp = p[0] < 0x80 ? p[0] : p[0] < 0xE0 ? ((p[0] & 0x1F) << 6) | (p[1] & 0x3F) : ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    wanted.push_back(cp);
  }
  checkFont(ugly_52, wanted, "ugly_52");
}

TEST(Font, BodySizesStartAt28) {
  // The hook of ư and ơ is lost below 28 px: the sizes that carry sentences and rows are 30 and 38.
  const auto& g = ugly_30.glyph[0];
  (void)g;
  EXPECT_GE(ugly_30.advanceY, 38);
  EXPECT_GE(ugly_38.advanceY, ugly_30.advanceY);
}

}  // namespace
