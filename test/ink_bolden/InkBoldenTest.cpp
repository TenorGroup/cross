#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "EpdFont/InkBolden.h"
#include "EpdFont/builtinFonts/notoserif_16_regular.h"

namespace {
using Pixels = std::vector<std::vector<int>>;

int readPixel(const uint8_t* bitmap, int index, bool twoBit) {
  const int bits = twoBit ? 2 : 1;
  const int offset = index * bits;
  return (bitmap[offset / 8] >> (8 - bits - offset % 8)) & ((1 << bits) - 1);
}

void writePixel(uint8_t* bitmap, int index, bool twoBit, int value) {
  const int bits = twoBit ? 2 : 1;
  const int offset = index * bits;
  const int shift = 8 - bits - offset % 8;
  const int mask = ((1 << bits) - 1) << shift;
  bitmap[offset / 8] = (bitmap[offset / 8] & ~mask) | (value << shift);
}

std::vector<uint8_t> pack(const Pixels& pixels, bool twoBit) {
  const int width = pixels.front().size();
  const int height = pixels.size();
  std::vector<uint8_t> bitmap((width * height * (twoBit ? 2 : 1) + 7) / 8, 0);
  for (int row = 0; row < height; ++row)
    for (int column = 0; column < width; ++column)
      writePixel(bitmap.data(), row * width + column, twoBit, pixels[row][column]);
  return bitmap;
}

Pixels reference(const Pixels& input, bool twoBit, int level, bool antiAliased) {
  const int height = input.size();
  const int width = input.front().size();
  Pixels current = input;
  for (int axis = 0; axis < 2; ++axis) {
    const Pixels source = current;
    const int left = twoBit ? level / 2 : level / 4;
    const int right = twoBit ? (level + 1) / 2 : (level + 2) / 4;
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        auto sample = [&](int displacement) {
          const int sourceRow = row - (axis == 1 ? displacement : 0);
          const int sourceColumn = column - (axis == 0 ? displacement : 0);
          return sourceRow >= 0 && sourceRow < height && sourceColumn >= 0 && sourceColumn < width
                     ? source[sourceRow][sourceColumn]
                     : 0;
        };
        int candidate = 0;
        for (int shift = -left; shift <= right; ++shift) {
          if (shift == 0) continue;
          int value;
          if (!twoBit) {
            value = sample(shift);
          } else if (shift % 2 == 0) {
            value = sample(shift >= 0 ? shift / 2 : -((-shift) / 2));
          } else {
            const int lower = shift >= 0 ? shift / 2 : -((-shift + 1) / 2);
            value = (sample(lower) + sample(lower + 1) + 1) / 2;
          }
          candidate = std::max(candidate, value);
        }
        if (twoBit && !antiAliased && candidate < 2) candidate = 0;
        current[row][column] = std::max(source[row][column], candidate);
      }
    }
  }
  return current;
}

void expectPixels(const uint8_t* bitmap, const Pixels& expected, bool twoBit) {
  const int width = expected.front().size();
  for (int row = 0; row < static_cast<int>(expected.size()); ++row)
    for (int column = 0; column < width; ++column)
      ASSERT_EQ(readPixel(bitmap, row * width + column, twoBit), expected[row][column])
          << "row=" << row << " column=" << column;
}
}

TEST(InkBolden, LevelZeroPreservesEveryByte) {
  for (bool twoBit : {false, true}) {
    std::array<uint8_t, 5> bitmap{0xA5, 0xFF, 0x73, 0x00, 0xDA};
    const auto original = bitmap;
    EXPECT_FALSE(inkBolden::apply(bitmap.data(), 3, 5, twoBit, 0, true));
    EXPECT_EQ(bitmap, original);
  }
}

TEST(InkBolden, EmptyAndInvalidInputsPreserveBytes) {
  uint8_t bitmap = 0xA5;
  for (const auto& dimensions : {std::array<int, 2>{0, 3}, {3, 0}, {-1, 3}, {256, 1}, {1, 256}})
    EXPECT_FALSE(inkBolden::apply(&bitmap, dimensions[0], dimensions[1], true, 1, true));
  EXPECT_FALSE(inkBolden::apply(nullptr, 1, 1, true, 1, true));
  EXPECT_FALSE(inkBolden::apply(&bitmap, 1, 1, true, 6, true));
  EXPECT_EQ(bitmap, 0xA5);
}

TEST(InkBolden, MatchesIndependentReferenceOn2000RandomBitmaps) {
  std::mt19937 random(0x56A);
  for (int iteration = 0; iteration < 2000; ++iteration) {
    const int width = 1 + random() % 37;
    const int height = 1 + random() % 30;
    for (bool twoBit : {false, true}) {
      Pixels original(height, std::vector<int>(width));
      for (auto& row : original)
        for (auto& pixel : row) pixel = random() % (twoBit ? 4 : 2);
      for (int level = 1; level <= 5; ++level) {
        for (bool antiAliased : {false, true}) {
          SCOPED_TRACE(::testing::Message() << "iteration=" << iteration << " width=" << width
                                           << " height=" << height << " twoBit=" << twoBit
                                           << " level=" << level << " AA=" << antiAliased);
          auto bitmap = pack(original, twoBit);
          ASSERT_TRUE(inkBolden::apply(bitmap.data(), width, height, twoBit, level, antiAliased));
          expectPixels(bitmap.data(), reference(original, twoBit, level, antiAliased), twoBit);
          if (::testing::Test::HasFatalFailure()) return;
        }
      }
    }
  }
}

TEST(InkBolden, HandCalculatedHalfPixelRows) {
  const Pixels original{{0, 0, 3, 3, 0, 0}};
  const std::array<std::array<int, 6>, 3> expected{{{0, 0, 3, 3, 2, 0},
                                                {0, 2, 3, 3, 2, 0},
                                                {0, 3, 3, 3, 3, 0}}};
  const std::array<int, 3> levels{1, 2, 4};
  for (int index = 0; index < 3; ++index) {
    for (bool antiAliased : {false, true}) {
      auto bitmap = pack(original, true);
      ASSERT_TRUE(inkBolden::apply(bitmap.data(), 6, 1, true, levels[index], antiAliased));
      expectPixels(bitmap.data(), Pixels{std::vector<int>(expected[index].begin(), expected[index].end())}, true);
    }
  }
  for (bool antiAliased : {false, true}) {
    auto bitmap = pack(Pixels{{1, 0}}, true);
    ASSERT_TRUE(inkBolden::apply(bitmap.data(), 2, 1, true, 1, antiAliased));
    expectPixels(bitmap.data(), Pixels{{1, antiAliased ? 1 : 0}}, true);
  }
}

TEST(InkBolden, IntegerPixelOneBitRowsIgnoreAA) {
  const Pixels original{{0, 0, 1, 0, 0}};
  const std::array<std::array<int, 5>, 5> expected{{{0, 0, 1, 0, 0}, {0, 0, 1, 1, 0},
                                                {0, 0, 1, 1, 0}, {0, 1, 1, 1, 0}, {0, 1, 1, 1, 0}}};
  for (int level = 1; level <= 5; ++level) {
    for (bool antiAliased : {false, true}) {
      auto bitmap = pack(original, false);
      ASSERT_TRUE(inkBolden::apply(bitmap.data(), 5, 1, false, level, antiAliased));
      expectPixels(bitmap.data(), Pixels{std::vector<int>(expected[level - 1].begin(), expected[level - 1].end())}, false);
    }
  }
}

TEST(InkBolden, GuardsAndPackedRowPaddingRemainIntact) {
  for (bool twoBit : {false, true}) {
    for (int width : {1, 3, 5, 7, 9, 37, 255}) {
      for (int height : {1, 3, 30, 255}) {
        Pixels original(height, std::vector<int>(width));
        for (int row = 0; row < height; ++row)
          for (int column = 0; column < width; ++column)
            original[row][column] = (row + column) % (twoBit ? 4 : 2);
        const auto packed = pack(original, twoBit);
        const int usedBits = width * height * (twoBit ? 2 : 1);
        const int unusedBits = (8 - usedBits % 8) % 8;
        const uint8_t paddingMask = (1 << unusedBits) - 1;
        for (int level = 1; level <= 5; ++level) {
          for (bool antiAliased : {false, true}) {
            std::vector<uint8_t> guarded(packed.size() + 16, 0xA5);
            std::copy(packed.begin(), packed.end(), guarded.begin() + 8);
            guarded[8 + packed.size() - 1] |= paddingMask;
            ASSERT_TRUE(inkBolden::apply(guarded.data() + 8, width, height, twoBit, level, antiAliased));
            expectPixels(guarded.data() + 8, reference(original, twoBit, level, antiAliased), twoBit);
            for (int index = 0; index < 8; ++index) {
              EXPECT_EQ(guarded[index], 0xA5);
              EXPECT_EQ(guarded[8 + packed.size() + index], 0xA5);
            }
            EXPECT_EQ(guarded[8 + packed.size() - 1] & paddingMask, paddingMask);
          }
        }
      }
    }
  }
}

TEST(InkBolden, RealNotoSerifGlyphsPreserveMetrics) {
  const auto& font = notoserif_16_regular;
  for (uint32_t codepoint : {0x6Cu, 0x6Du, 0x111u}) {
    SCOPED_TRACE(codepoint);
    int glyphIndex = -1;
    for (const auto& interval : notoserif_16_regularIntervals)
      if (codepoint >= interval.first && codepoint <= interval.last)
        glyphIndex = interval.offset + codepoint - interval.first;
    ASSERT_GE(glyphIndex, 0);
    const auto& glyph = font.glyph[glyphIndex];
    const auto originalMetrics = glyph;
    const EpdFontGroup* group = nullptr;
    for (const auto& entry : notoserif_16_regularGroups)
      if (static_cast<uint32_t>(glyphIndex) >= entry.firstGlyphIndex &&
          static_cast<uint32_t>(glyphIndex) < entry.firstGlyphIndex + entry.glyphCount)
        group = &entry;
    ASSERT_NE(group, nullptr);
    std::vector<uint8_t> unpacked(group->uncompressedSize);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(font.bitmap + group->compressedOffset);
    stream.avail_in = group->compressedSize;
    stream.next_out = unpacked.data();
    stream.avail_out = unpacked.size();
    ASSERT_EQ(inflateInit2(&stream, -MAX_WBITS), Z_OK);
    const int result = inflate(&stream, Z_FINISH);
    const auto outputSize = stream.total_out;
    EXPECT_EQ(inflateEnd(&stream), Z_OK);
    ASSERT_EQ(result, Z_STREAM_END);
    ASSERT_EQ(outputSize, group->uncompressedSize);
    ASSERT_LE(glyph.dataOffset + glyph.dataLength, unpacked.size());
    const int bytes = (glyph.width * glyph.height * 2 + 7) / 8;
    ASSERT_EQ(glyph.dataLength, bytes);
    std::vector<uint8_t> original(unpacked.begin() + glyph.dataOffset,
                                  unpacked.begin() + glyph.dataOffset + bytes);
    Pixels pixels(glyph.height, std::vector<int>(glyph.width));
    for (int row = 0; row < glyph.height; ++row)
      for (int column = 0; column < glyph.width; ++column)
        pixels[row][column] = readPixel(original.data(), row * glyph.width + column, true);
    for (int level = 1; level <= 5; ++level) {
      for (bool antiAliased : {false, true}) {
        auto bitmap = original;
        ASSERT_TRUE(inkBolden::apply(bitmap.data(), glyph.width, glyph.height, true, level, antiAliased));
        expectPixels(bitmap.data(), reference(pixels, true, level, antiAliased), true);
      }
    }
    EXPECT_EQ(glyph.width, originalMetrics.width);
    EXPECT_EQ(glyph.height, originalMetrics.height);
    EXPECT_EQ(glyph.advanceX, originalMetrics.advanceX);
    EXPECT_EQ(glyph.left, originalMetrics.left);
    EXPECT_EQ(glyph.top, originalMetrics.top);
  }
}

TEST(InkBolden, HostTiming50Glyphs20x26Level5) {
  constexpr int glyphCount = 50;
  constexpr int batches = 200;
  std::mt19937 random(0x56A);
  std::array<std::array<uint8_t, 130>, glyphCount> original{};
  for (auto& glyph : original)
    for (auto& byte : glyph) byte = random();
  auto working = original;
  std::chrono::nanoseconds elapsed{0};
  uint64_t checksum = 0;
  for (int batch = 0; batch < batches; ++batch) {
    working = original;
    const auto start = std::chrono::steady_clock::now();
    for (auto& glyph : working) inkBolden::apply(glyph.data(), 20, 26, true, 5, true);
    elapsed += std::chrono::steady_clock::now() - start;
    for (const auto& glyph : working) checksum += glyph.back();
  }
  std::cout << "host ns/glyph=" << elapsed.count() / (glyphCount * batches)
            << " glyphs/batch=" << glyphCount << " batches=" << batches << " checksum=" << checksum << '\n';
  EXPECT_GT(checksum, 0u);
}
