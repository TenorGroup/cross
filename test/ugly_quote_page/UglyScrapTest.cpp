// tenor/ugly quotation page, the scribbles: none of them touches a word, the same page scribbles the same, the room under the
// words decides how many, and the wall drawing comes at most once.
#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

#include "shells/ugly/UglyScrap.h"

namespace {
using namespace ugly::scrap;

constexpr Rect AREA{44, 16, 508, 704};

// A page of `rows` rows of the quote under the quip, the way QuoteDetailActivity keeps them clear.
std::vector<Rect> words(const int rows, const int lastWidth = 300) {
  std::vector<Rect> out;
  out.push_back({42, 16 + 38, 502, 16 + 116});  // the line of abuse and its rule
  out.push_back({36, 16 + 116, 84, 16 + 164});  // the opening mark
  for (int row = 0; row < rows; ++row) {
    const int baseline = 16 + 188 + row * 40;
    out.push_back({42, baseline - 36, row + 1 == rows ? 48 + lastWidth : 496, baseline + 16});
  }
  return out;
}

std::vector<Item> run(const std::vector<Rect>& avoid, const uint32_t seed, const Rect& area = AREA) {
  Item items[MAX_ITEMS];
  const int n = place(area, avoid.data(), static_cast<int>(avoid.size()), seed, items, MAX_ITEMS);
  return std::vector<Item>(items, items + n);
}

uint64_t hashOf(const std::vector<Item>& items) {
  uint64_t h = 1469598103934665603ull;
  for (const auto& i : items)
    for (int v : {static_cast<int>(i.kind), static_cast<int>(i.pic), static_cast<int>(i.pct), static_cast<int>(i.mirror),
                  static_cast<int>(i.cross), i.box.x0, i.box.y0, i.box.x1, i.box.y1, i.x, i.y, i.w, i.h})
      h = (h ^ static_cast<uint32_t>(v)) * 1099511628211ull;
  return h;
}

TEST(Scrap, NoScribbleTouchesAWordOrAnotherScribbleAndAllStayInTheArea) {
  uint32_t state = 99;
  for (int round = 0; round < 3000; ++round) {
    const int rows = static_cast<int>(next(state) % 14);
    const auto avoid = words(rows, 20 + static_cast<int>(next(state) % 440));
    const auto items = run(avoid, next(state));
    for (size_t a = 0; a < items.size(); ++a) {
      const Rect& box = items[a].box;
      ASSERT_GE(box.x0, AREA.x0);
      ASSERT_GE(box.y0, AREA.y0);
      ASSERT_LE(box.x1, AREA.x1);
      ASSERT_LE(box.y1, AREA.y1);
      // 14 px of air round every word: the number the page's own design asks for, written here so a smaller CLEAR fails this
      for (const auto& w : avoid) ASSERT_FALSE(meets(box, grown(w, 14))) << "round " << round << " item " << a;
      for (size_t b = a + 1; b < items.size(); ++b) ASSERT_FALSE(meets(box, grown(items[b].box, 4))) << round;
    }
  }
}

TEST(Scrap, TheSamePageScribblesTheSameAndAnotherPageDoesNot) {
  const auto avoid = words(3);
  EXPECT_EQ(hashOf(run(avoid, seedOf(0xabcdef, 0))), hashOf(run(avoid, seedOf(0xabcdef, 0))));
  std::set<uint64_t> seen;
  for (int page = 0; page < 6; ++page) seen.insert(hashOf(run(avoid, seedOf(0xabcdef, page))));
  EXPECT_EQ(seen.size(), 6u);
  seen.clear();
  for (uint64_t quote = 1; quote <= 40; ++quote) seen.insert(hashOf(run(avoid, seedOf(quote * 0x10000001b3ull, 0))));
  EXPECT_GE(seen.size(), 38u);
}

TEST(Scrap, APageFullOfWordsGetsNone) {
  for (uint32_t seed = 1; seed < 200; ++seed) {
    EXPECT_EQ(run(words(12), seed).size(), 0u) << seed;
    EXPECT_EQ(run(words(13), seed).size(), 0u) << seed;
  }
}

TEST(Scrap, RoomUnderTheWordsDecidesHowMany) {
  const auto mean = [&](const int rows) {
    int total = 0;
    for (uint32_t seed = 1; seed <= 100; ++seed) total += static_cast<int>(run(words(rows), seedOf(seed, 0)).size());
    return total / 100.0;
  };
  const double empty = mean(0), few = mean(3), many = mean(8), packed = mean(10);
  EXPECT_GT(empty, few);
  EXPECT_GT(few, many);
  EXPECT_GE(many, packed);
  EXPECT_GE(empty, 5.0);
  EXPECT_LE(empty, MAX_ITEMS);
  EXPECT_GE(few, 3.0);
}

TEST(Scrap, ShortPageAlwaysHasSomethingOnIt) {
  for (uint32_t seed = 1; seed <= 300; ++seed) EXPECT_GE(run(words(3), seedOf(seed, 1)).size(), 3u) << seed;
}

TEST(Scrap, TheWallDrawingComesOnceAtMostAndOnManyPages) {
  int pages = 0;
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    int wall = 0;
    for (const auto& i : run(words(3), seedOf(seed, 0))) wall += i.kind == Kind::Doodle && i.pic == ugly::doodledata::WALL;
    ASSERT_LE(wall, 1) << seed;
    pages += wall;
  }
  EXPECT_GE(pages, 400 * 25 / 100);
  EXPECT_LE(pages, 400 * 75 / 100);
}

TEST(Scrap, EveryKindAndPictureTurnsUpAndFlipsBothWays) {
  std::set<int> kinds, doodles;
  int flipped = 0, straight = 0;
  for (uint32_t seed = 1; seed <= 400; ++seed)
    for (const auto& i : run(words(0), seedOf(seed, 0))) {
      kinds.insert(static_cast<int>(i.kind));
      if (i.kind == Kind::Doodle) doodles.insert(i.pic);
      if (i.kind == Kind::Doodle || i.kind == Kind::Sleep) (i.mirror ? flipped : straight)++;
    }
  EXPECT_EQ(kinds.size(), 5u);
  EXPECT_EQ(doodles.size(), static_cast<size_t>(ugly::doodledata::COUNT));
  EXPECT_GT(flipped, 100);
  EXPECT_GT(straight, 100);
}

TEST(Scrap, ASeedIsAPageOfAQuote) {
  EXPECT_NE(seedOf(5, 0), seedOf(5, 1));
  EXPECT_NE(seedOf(5, 0), seedOf(6, 0));
  EXPECT_EQ(seedOf(0xffffffffffffffffull, 31), seedOf(0xffffffffffffffffull, 31));
}

TEST(Scrap, NothingIsPlacedWhenTheAreaIsTooNarrowOrCapIsZero) {
  EXPECT_EQ(run(words(0), 1, {44, 16, 120, 704}).size(), 0u);
  Item items[1];
  EXPECT_EQ(place(AREA, nullptr, 0, 1, items, 0), 0);
  EXPECT_EQ(place(AREA, nullptr, 0, 1, items, 1), 1);
}
}  // namespace
