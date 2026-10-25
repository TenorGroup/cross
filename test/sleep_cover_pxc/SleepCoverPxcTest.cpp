// A JPEG set as the sleep screen goes through the .pxc the converter streams (PixelCache.h): "PXC", a format byte,
// then width and height, then 2-bit rows. The BMP made from it must be the image, whatever the header length.
#include <Arduino.h>
#include <PixelCache.h>
#include <PxcSleepBmp.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {
constexpr char PXC[] = "/sleep.bmp.pxc.tmp";
constexpr char BMP[] = "/sleep.bmp.tmp";

// The levels of pixel (x, y): a pattern that tells every pixel from its neighbours.
uint8_t level(const int x, const int y) { return static_cast<uint8_t>((x + 2 * y) & 3); }

// The .pxc PixelCache writes for a width x height image of those levels.
void writePxc(const int width, const int height) {
  hostFiles.clear();
  PixelCache cache;
  ASSERT_TRUE(cache.begin(PXC, width, height, 0, 0, 1));
  for (int y = 0; y < height; ++y) {
    ASSERT_TRUE(cache.advanceTo(y));
    for (int x = 0; x < width; ++x) {
      uint8_t& packed = cache.buffer[x / 4];
      packed = static_cast<uint8_t>(packed | level(x, y) << (6 - (x & 3) * 2));
    }
  }
  ASSERT_TRUE(cache.finalize());
}

int32_t le32(const std::vector<uint8_t>& b, const size_t at) {
  return static_cast<int32_t>(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<uint32_t>(b[at + 3]) << 24);
}
}  // namespace

TEST(SleepCoverPxc, TheBmpIsTheImageThePixelCacheWrote) {
  for (const auto& size : {std::pair{20, 10}, std::pair{33, 7}, std::pair{16, 16}}) {
    const int width = size.first, height = size.second;
    writePxc(width, height);
    ASSERT_TRUE(pxcsleep::toBmp(PXC, BMP, width, height)) << width << "x" << height;
    const auto& bmp = hostFiles.at(BMP);
    const size_t offset = le32(bmp, 10);
    EXPECT_EQ(le32(bmp, 18), width);
    EXPECT_EQ(le32(bmp, 22), -height) << "top-down";
    const size_t rowBytes = (width + 15) / 16 * 4;
    ASSERT_EQ(bmp.size(), offset + rowBytes * height);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        EXPECT_EQ((bmp[offset + y * rowBytes + x / 4] >> (6 - (x & 3) * 2)) & 3, level(x, y)) << x << "," << y;
  }
}

TEST(SleepCoverPxc, AnImageOfAnotherSizeOrACutFileIsRefused) {
  writePxc(20, 10);
  EXPECT_TRUE(pxcsleep::toBmp(PXC, BMP, 20, 10));
  EXPECT_FALSE(pxcsleep::toBmp(PXC, BMP, 20, 11));
  EXPECT_FALSE(pxcsleep::toBmp(PXC, BMP, 21, 10));
  hostFiles.at(PXC).pop_back();
  EXPECT_FALSE(pxcsleep::toBmp(PXC, BMP, 20, 10));
}
