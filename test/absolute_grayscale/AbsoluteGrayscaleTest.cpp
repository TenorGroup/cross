#include <gtest/gtest.h>

#include "lib/GfxRenderer/BitmapHelpers.h"

namespace {
thread_local int rowAllocationToFail = -1;
}

// Fail one row allocation without changing production allocator APIs.
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (rowAllocationToFail >= 0 && rowAllocationToFail-- == 0) return nullptr;
  return ::operator new[](size);
}

// Each ditherer takes its error rows in one allocation, so one failed allocation leaves it invalid.
TEST(AbsoluteGrayscale, DitherersReportScratchAllocationFailure) {
  rowAllocationToFail = 0;
  AtkinsonDitherer failedAtkinson(8);
  rowAllocationToFail = -1;
  EXPECT_FALSE(failedAtkinson.isValid());

  rowAllocationToFail = 0;
  FloydSteinberg1BitDitherer failedOneBit(8);
  rowAllocationToFail = -1;
  EXPECT_FALSE(failedOneBit.isValid());

  rowAllocationToFail = 0;
  FloydSteinbergDitherer failedFloyd(8);
  rowAllocationToFail = -1;
  EXPECT_FALSE(failedFloyd.isValid());
  AtkinsonDitherer atkinson(8);
  FloydSteinberg1BitDitherer oneBit(8);
  FloydSteinbergDitherer floyd(8);
  EXPECT_TRUE(atkinson.isValid());
  EXPECT_TRUE(oneBit.isValid());
  EXPECT_TRUE(floyd.isValid());
}

TEST(AbsoluteGrayscale, FullPlanesIncludeBlackWhiteAndBothGrayLevels) {
  uint8_t planes[2] = {0xff, 0xff};
  for (unsigned p = 0; p < 2; ++p) {
    for (unsigned x = 0; x < 8; ++x) {
      const auto pixel = grayPlanePixel(x % 4, p == 1, true);
      ASSERT_TRUE(pixel.write);
      if (pixel.black)
        planes[p] &= ~(0x80 >> x);
      else
        planes[p] |= 0x80 >> x;
    }
  }
  EXPECT_EQ(planes[0], 0x55);  // black/dark/light/white = 0/1/0/1
  EXPECT_EQ(planes[1], 0x33);  // black/dark/light/white = 0/0/1/1
}

TEST(AbsoluteGrayscale, OverlayStillLeavesBlackAndWhiteToTheBase) {
  uint8_t planes[2] = {0, 0};
  for (unsigned p = 0; p < 2; ++p) {
    for (unsigned x = 0; x < 8; ++x) {
      const auto pixel = grayPlanePixel(x % 4, p == 1, false);
      if (x % 4 == 0 || x % 4 == 3) EXPECT_FALSE(pixel.write);
      if (pixel.write && !pixel.black) planes[p] |= 0x80 >> x;
    }
  }
  EXPECT_EQ(planes[0], 0x44);
  EXPECT_EQ(planes[1], 0x66);
}

TEST(AbsoluteGrayscale, ImageQuantizersRetainFourEvenLevels) {
  for (uint8_t level = 0; level < 4; ++level) {
    AtkinsonDitherer atkinson(1, true);
    FloydSteinbergDitherer floyd(1, true);
    EXPECT_EQ(atkinson.processPixel(level * 85, 0), level);
    EXPECT_EQ(floyd.processPixel(level * 85, 0), level);
  }
  AtkinsonDitherer overlay(1);
  EXPECT_EQ(overlay.processPixel(85, 0), 2);
}

TEST(AbsoluteGrayscale, TransparentPassesRetainBackgroundWithoutClearingBetweenPlanes) {
  // Alternating B/W background, with four opaque pixels and four transparent ones.
  uint8_t frame = 0xaa;
  uint8_t planes[2] = {};
  for (unsigned plane = 0; plane < 2; ++plane) {
    for (unsigned x = 0; x < 4; ++x) {
      const auto pixel = grayPlanePixel(x, plane == 1, true);
      if (pixel.black)
        frame &= ~(0x80 >> x);
      else
        frame |= 0x80 >> x;
    }
    planes[plane] = frame;
  }
  EXPECT_EQ(planes[0], 0x5a);
  EXPECT_EQ(planes[1], 0x3a);
  EXPECT_EQ(planes[0] & 0x0f, 0x0a);
  EXPECT_EQ(planes[1] & 0x0f, 0x0a);
}

TEST(AbsoluteGrayscale, PackedRowsMatchPixelMappingInEveryOrientation) {
  uint8_t levels[4] = {0x1b, 0xe4, 0x55, 0xaa};
  uint8_t lsb[64], msb[64], expectedLsb[64], expectedMsb[64];
  const int starts[] = {7, 31, 480, 15};
  const int steps[] = {1, -1, -32, 32};
  for (int direction = 0; direction < 4; ++direction) {
    for (int i = 0; i < 64; ++i) lsb[i] = msb[i] = expectedLsb[i] = expectedMsb[i] = 0xff;
    writeAbsoluteGrayRow(levels, 16, starts[direction], steps[direction], lsb, msb);
    for (int x = 0; x < 16; ++x) {
      const int bit = starts[direction] + x * steps[direction];
      const uint8_t mask = 0x80 >> (bit % 8);
      const int value = (levels[x / 4] >> (6 - (x % 4) * 2)) & 3;
      if (grayPlanePixel(value, false, true).black) expectedLsb[bit / 8] &= ~mask;
      if (grayPlanePixel(value, true, true).black) expectedMsb[bit / 8] &= ~mask;
    }
    for (int i = 0; i < 64; ++i) {
      EXPECT_EQ(lsb[i], expectedLsb[i]);
      EXPECT_EQ(msb[i], expectedMsb[i]);
    }
  }
}

// The 1-bit dither keeps a flat tone: its share of white matches the tone within a few levels, from
// deep shadow to near paper. Atkinson lost 32 to black and 224 to white.
TEST(AbsoluteGrayscale, OneBitDitherKeepsFlatTones) {
  constexpr int W = 64, H = 64;
  for (const int tone : {16, 32, 64, 96, 128, 160, 192, 224, 240}) {
    FloydSteinberg1BitDitherer dither(W);
    ASSERT_TRUE(dither.isValid());
    int white = 0;
    for (int y = 0; y < H; ++y) {
      for (int i = 0; i < W; ++i) white += dither.processPixel(tone, dither.at(i));
      dither.nextRow();
    }
    EXPECT_NEAR(white * 255.0 / (W * H), tone, 4.0) << "tone " << tone;
  }
}

// Odd rows run right to left: the error of a row's last pixel lands on the next row's first.
TEST(AbsoluteGrayscale, OneBitDitherRunsSerpentine) {
  FloydSteinberg1BitDitherer dither(5);
  for (int i = 0; i < 5; ++i) EXPECT_EQ(dither.at(i), i);
  dither.nextRow();
  for (int i = 0; i < 5; ++i) EXPECT_EQ(dither.at(i), 4 - i);
}
