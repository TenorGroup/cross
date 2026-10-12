// The scribbles of the quotation page: the ink boxes baked beside their strokes are the boxes the strokes draw in, and a
// scribble placed on a page draws inside the box the placement kept clear.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "components/X3BrandCodec.h"
#include "shells/ugly/UglyDoodleData.h"
#include "shells/ugly/UglyScrap.h"
#include "shells/ugly/UglySleepData.h"
#include "shells/ugly/UglySleepSet.h"

namespace {
namespace dd = ugly::doodledata;
using ugly::scrap::Item;
using ugly::scrap::Kind;

template <size_t N>
std::vector<uint8_t> unpack(const uint8_t (&packed)[N], const unsigned raw) {
  std::vector<uint8_t> out(raw);
  EXPECT_TRUE(decodeX3BrandPlane(packed, N, out.data(), raw));
  return out;
}

struct Seg {
  int x0, y0, x1, y1, w;
};

std::vector<Seg> strokes(const std::vector<uint8_t>& stream, const int pic) {
  std::vector<Seg> out;
  EXPECT_TRUE(ugly::sleepset::drawPicture(stream.data(), stream.size(), pic, 0, 0,
                                          [&](int a, int b, int c, int d, int, int w) { out.push_back({a, b, c, d, w}); }));
  return out;
}

dd::Box inkOf(const std::vector<Seg>& segs) {
  dd::Box b{32000, 32000, -32000, -32000};
  for (const auto& s : segs) {
    b.x0 = std::min<int>(b.x0, std::min(s.x0, s.x1) - 3);
    b.y0 = std::min<int>(b.y0, std::min(s.y0, s.y1) - 3);
    b.x1 = std::max<int>(b.x1, std::max(s.x0, s.x1) + 4);
    b.y1 = std::max<int>(b.y1, std::max(s.y0, s.y1) + 4);
  }
  return b;
}

TEST(Doodles, TheBakedBoxIsTheBoxOfTheStrokesForTheScribblesAndForTheSleepDoodles) {
  const auto doodles = unpack(dd::PICTURES, dd::PICTURES_RAW);
  const auto sleep = unpack(ugly::sleepdata::PICTURES, ugly::sleepdata::PICTURES_RAW);
  ASSERT_EQ(doodles[0], dd::COUNT);
  for (int pic = 0; pic < dd::COUNT; ++pic) {
    const auto ink = inkOf(strokes(doodles, pic));
    EXPECT_EQ(dd::BOX[pic].x0, ink.x0) << pic;
    EXPECT_EQ(dd::BOX[pic].y0, ink.y0) << pic;
    EXPECT_EQ(dd::BOX[pic].x1, ink.x1) << pic;
    EXPECT_EQ(dd::BOX[pic].y1, ink.y1) << pic;
  }
  for (int pic = 0; pic < 8; ++pic) {
    const auto ink = inkOf(strokes(sleep, pic));
    EXPECT_EQ(dd::SLEEP_BOX[pic].x0, ink.x0) << pic;
    EXPECT_EQ(dd::SLEEP_BOX[pic].y0, ink.y0) << pic;
    EXPECT_EQ(dd::SLEEP_BOX[pic].x1, ink.x1) << pic;
    EXPECT_EQ(dd::SLEEP_BOX[pic].y1, ink.y1) << pic;
  }
}

TEST(Doodles, APlacedPictureDrawsInsideTheBoxKeptClearForIt) {
  const auto doodles = unpack(dd::PICTURES, dd::PICTURES_RAW);
  const auto sleep = unpack(ugly::sleepdata::PICTURES, ugly::sleepdata::PICTURES_RAW);
  int checked = 0;
  for (uint32_t seed = 1; seed <= 300; ++seed) {
    Item items[ugly::scrap::MAX_ITEMS];
    const int n = ugly::scrap::place({44, 16, 508, 704}, nullptr, 0, ugly::scrap::seedOf(seed, 0), items, ugly::scrap::MAX_ITEMS);
    for (int i = 0; i < n; ++i) {
      const Item& item = items[i];
      if (item.kind != Kind::Doodle && item.kind != Kind::Sleep) continue;
      for (const auto& s : strokes(item.kind == Kind::Doodle ? doodles : sleep, item.pic)) {
        // `line` of the pen may stray 1 px off a long segment, a dot is 3 wide: the box has a pen's width of its own.
        for (const auto [x, y] : {std::pair{s.x0, s.y0}, std::pair{s.x1, s.y1}}) {
          const auto at = ugly::scrap::map(item, x, y);
          ASSERT_GE(at.x - 2, item.box.x0) << seed << " " << i;
          ASSERT_LE(at.x + 2, item.box.x1) << seed << " " << i;
          ASSERT_GE(at.y - 2, item.box.y0) << seed << " " << i;
          ASSERT_LE(at.y + 2, item.box.y1) << seed << " " << i;
        }
      }
      ++checked;
    }
  }
  EXPECT_GT(checked, 500);
}
}  // namespace
