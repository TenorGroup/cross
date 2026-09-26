// Page images shrink through the JPEG converter's own filter (JpegToFramebufferConverter.cpp).
// The X3 draws a 780 x 1227 picture at 482 x 759: a factor of 0,618 that the decoder's 1/2 step
// cannot take, so every output pixel comes from the converter. Picking one source pixel per
// output pixel dropped 38 % of the rows and columns, and fine lines turned into bands. The filter
// now blends the two source rows and columns around each output pixel's center; its rows and
// columns can sit in two decoder blocks (an MCU row is 16 source rows, cut into pieces 128 or
// 256 wide), and an edge that repeats one side instead of blending draws a line every block.
#include <Arduino.h>
#include <DitherUtils.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "JpegToFramebufferConverter.h"

EspClass ESP;

namespace {

constexpr int SRC_W = 780, SRC_H = 1227, DST_W = 482, DST_H = 759;
const std::string CACHE = "/cache/img.pxc";

// A triangle wave: straight between its corners, so between two samples the right value is the
// line through them, and a row or column that repeats one sample is off by up to `slope`.
int triangle(const int t, const int period) {
  const int half = period / 2;
  const int phase = t % period;
  return (phase < half ? phase : period - phase) * 255 / half;
}

// Offset a quarter period so a block edge (every 16 or 8 source rows, 128 or 256 columns) falls
// mid-slope, where a repeated sample is furthest off; at a trough it would stay black either way.
void setImage(const bool alongRows, const int period) {
  hostJpeg.width = SRC_W;
  hostJpeg.height = SRC_H;
  hostJpeg.gray.assign(static_cast<size_t>(SRC_W) * SRC_H, 0);
  for (int y = 0; y < SRC_H; y++)
    for (int x = 0; x < SRC_W; x++) hostJpeg.gray[y * SRC_W + x] = triangle((alongRows ? y : x) + period / 4, period);
}

// The 2-bit levels the converter streamed to the pixel cache.
std::vector<uint8_t> decode(const int blockRows, const int blockCols) {
  hostJpeg.blockRows = blockRows;
  hostJpeg.blockCols = blockCols;
  hostFiles.clear();
  GfxRenderer renderer;
  RenderConfig config{};
  config.x = 0;
  config.y = 0;
  config.maxWidth = DST_W;
  config.maxHeight = DST_H;
  config.useExactDimensions = true;
  config.cachePath = CACHE;
  JpegToFramebufferConverter converter;
  EXPECT_TRUE(converter.decodeToFramebuffer("/page.jpg", renderer, config));
  const auto& file = hostFiles[CACHE];
  const size_t header = file.size() >= 3 && file[0] == 'P' && file[1] == 'X' && file[2] == 'C' ? 8 : 4;
  const int bytesPerRow = (DST_W + 3) / 4;
  std::vector<uint8_t> levels(static_cast<size_t>(DST_W) * DST_H, 0);
  if (file.size() < header + static_cast<size_t>(bytesPerRow) * DST_H) return {};
  for (int y = 0; y < DST_H; y++)
    for (int x = 0; x < DST_W; x++)
      levels[y * DST_W + x] = (file[header + y * bytesPerRow + x / 4] >> (6 - (x & 3) * 2)) & 3;
  return levels;
}

// The levels a source sampled between its pixels (center aligned) and dithered like the page gives,
// as a mask of the levels a gray within two steps of the exact value dithers to: the filter works
// in 16.16 steps, and one gray step moves a whole flat row across a dither threshold.
std::vector<uint8_t> expected() {
  std::vector<uint8_t> levels(static_cast<size_t>(DST_W) * DST_H);
  const double sx = static_cast<double>(SRC_W) / DST_W, sy = static_cast<double>(SRC_H) / DST_H;
  const auto at = [](int x, int y) {
    x = std::min(std::max(x, 0), SRC_W - 1);
    y = std::min(std::max(y, 0), SRC_H - 1);
    return static_cast<double>(hostJpeg.gray[y * SRC_W + x]);
  };
  for (int y = 0; y < DST_H; y++) {
    const double fy = (y + 0.5) * sy - 0.5;
    const int y0 = static_cast<int>(std::floor(fy));
    for (int x = 0; x < DST_W; x++) {
      const double fx = (x + 0.5) * sx - 0.5;
      const int x0 = static_cast<int>(std::floor(fx));
      const double wx = fx - x0, wy = fy - y0;
      const double top = at(x0, y0) * (1 - wx) + at(x0 + 1, y0) * wx;
      const double bottom = at(x0, y0 + 1) * (1 - wx) + at(x0 + 1, y0 + 1) * wx;
      const int gray = static_cast<int>(std::lround(top * (1 - wy) + bottom * wy));
      uint8_t mask = 0;
      for (int g = std::max(gray - 2, 0); g <= std::min(gray + 2, 255); g++)
        mask |= 1u << applyBayerDither4Level(static_cast<uint8_t>(g), x, y);
      levels[y * DST_W + x] = mask;
    }
  }
  return levels;
}

// Output rows (or columns) where more than 5 % of the pixels take a level outside the expected
// ones (a mask of levels, or one exact level when `exact`): a line across the image.
int streaks(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want, const bool rows,
            const bool exact = false) {
  int count = 0;
  const int lines = rows ? DST_H : DST_W, length = rows ? DST_W : DST_H;
  for (int i = 0; i < lines; i++) {
    int off = 0;
    for (int j = 0; j < length; j++) {
      const size_t at = rows ? static_cast<size_t>(i) * DST_W + j : static_cast<size_t>(j) * DST_W + i;
      off += exact ? got[at] != want[at] : !(want[at] >> got[at] & 1);
    }
    if (off * 20 > length) count++;
  }
  return count;
}

}  // namespace

TEST(PageImageDownscale, RowsFollowTheSourceBetweenItsSamples) {
  setImage(true, 16);
  const auto got = decode(16, 128);
  ASSERT_FALSE(got.empty());
  const int count = streaks(got, expected(), true);
  std::printf("row streaks: %d of %d\n", count, DST_H);
  EXPECT_EQ(count, 0);
}

TEST(PageImageDownscale, ColumnsFollowTheSourceBetweenItsSamples) {
  setImage(false, 16);
  const auto got = decode(16, 128);
  ASSERT_FALSE(got.empty());
  const int count = streaks(got, expected(), false);
  std::printf("column streaks: %d of %d\n", count, DST_W);
  EXPECT_EQ(count, 0);
}

// Every block shape the decoder hands over gives the image of one whole-width row at a time: no
// line at an MCU row edge (every 16 or 8 source rows) or at a piece edge (every 128 or 256 columns).
TEST(PageImageDownscale, BlockEdgesLeaveNoLine) {
  for (const bool alongRows : {true, false}) {
    setImage(alongRows, 16);
    const auto whole = decode(1, SRC_W);
    ASSERT_FALSE(whole.empty());
    for (const auto& shape : {std::pair{16, 128}, std::pair{16, 256}, std::pair{8, 128}, std::pair{8, 96}}) {
      const auto blocks = decode(shape.first, shape.second);
      ASSERT_FALSE(blocks.empty());
      EXPECT_EQ(streaks(blocks, whole, true, true), 0) << shape.first << "x" << shape.second << " rows " << alongRows;
      EXPECT_EQ(streaks(blocks, whole, false, true), 0) << shape.first << "x" << shape.second << " columns " << alongRows;
      EXPECT_EQ(blocks, whole) << shape.first << "x" << shape.second;
    }
  }
}
