#include <EpdFont.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <InkBolden.h>
#include <gtest/gtest.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/geist_10_regular.h>

#include <cstring>
#include <vector>
#include <zlib.h>

extern "C" uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t previous) {
  return adler32(previous, static_cast<const Bytef*>(data), length);
}
extern "C" uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t previous) {
  return ~crc32(~previous, static_cast<const Bytef*>(data), length);
}

namespace {
template <typename Manager>
void configure(Manager& manager, int fontId, uint8_t level, bool aa) {
  if constexpr (requires { manager.setReaderInk(fontId, level, aa); }) manager.setReaderInk(fontId, level, aa);
}
template <typename Stats>
uint32_t applications(const Stats& stats) {
  if constexpr (requires { stats.inkApplications; }) return stats.inkApplications;
  return 0;
}

struct BuiltinInk : testing::Test {
  EpdFont font{&notoserif_16_regular};
  EpdFont plain{&geist_10_regular};
  std::map<int, EpdFontFamily> families{{7, EpdFontFamily(&font)}, {8, EpdFontFamily(&plain)},
                                      {9, EpdFontFamily(&font)}};
  std::map<int, SdCardFont*> sd;
  std::map<int, TtfEpdFont*> ttf;
  FontDecompressor decompressor;
  FontCacheManager manager{families, sd, ttf};
  void SetUp() override { manager.setFontDecompressor(&decompressor); ASSERT_TRUE(decompressor.init()); }
  std::vector<uint8_t> bitmap(const EpdFont& face, uint32_t cp) {
    const auto* glyph = face.getGlyph(cp);
    const auto* data = face.data;
    const auto* bytes = decompressor.getBitmap(data, glyph, static_cast<uint32_t>(glyph - data->glyph));
    if (!bytes) return {};
    return {bytes, bytes + glyph->dataLength};
  }
};
}

TEST_F(BuiltinInk, PrewarmMatchesCoreAndMetricsRemainOriginal) {
  for (int level = 0; level < 6; ++level) for (bool aa : {false, true}) {
    configure(manager, 7, 0, aa);
    auto expected = bitmap(font, 'm');
    const auto original = *font.getGlyph('m');
    ASSERT_FALSE(expected.empty());
    inkBolden::apply(expected.data(), original.width, original.height, true, level, aa);
    configure(manager, 7, level, aa);
    manager.prewarmCache(7, "mmm", 1, false);
    EXPECT_EQ(bitmap(font, 'm'), expected) << level << ' ' << aa;
    EXPECT_EQ(std::memcmp(&original, font.getGlyph('m'), sizeof(original)), 0);
  }
}

TEST_F(BuiltinInk, HotGlyphMatchesPrewarmAndNeverAccumulates) {
  configure(manager, 7, 0, true);
  auto expected = bitmap(font, 'm');
  const auto* glyph = font.getGlyph('m');
  inkBolden::apply(expected.data(), glyph->width, glyph->height, true, 5, true);
  configure(manager, 7, 5, true);
  EXPECT_EQ(bitmap(font, 'm'), expected);
  EXPECT_EQ(bitmap(font, 'm'), expected);
  bitmap(font, 'l');
  EXPECT_EQ(bitmap(font, 'm'), expected);
  manager.prewarmCache(7, "m", 1, false);
  EXPECT_EQ(bitmap(font, 'm'), expected);
}

TEST_F(BuiltinInk, GrayPassesAndIdempotentApplyReuseTheSameSlot) {
  configure(manager, 7, 5, true);
  manager.prewarmCache(7, "mml", 1, false);
  const auto before = applications(decompressor.getStats());
  EXPECT_EQ(before, 2);
  const auto* glyph = font.getGlyph('m');
  auto* pointer = decompressor.getBitmap(font.data, glyph, glyph - font.data->glyph);
  for (int pass = 0; pass < 3; ++pass) {
    configure(manager, 7, 5, true);
    EXPECT_EQ(decompressor.getBitmap(font.data, glyph, glyph - font.data->glyph), pointer);
  }
  EXPECT_EQ(applications(decompressor.getStats()), before);
  configure(manager, 7, 4, true);
  EXPECT_EQ(decompressor.getStats().pageBufferBytes, 0);
  manager.prewarmCache(7, "m", 1, false);
  configure(manager, 7, 4, false);
  EXPECT_EQ(decompressor.getStats().pageBufferBytes, 0);
}

TEST_F(BuiltinInk, ExplicitPrewarmBoldsEachSharedFaceOnlyOnce) {
  configure(manager, 7, 5, true);
  manager.prewarmCache(7, "mmm", 15, false);
  EXPECT_EQ(applications(decompressor.getStats()), 1);
  decompressor.clearCache();
  decompressor.resetStats();
  manager.prewarmCache(7, "mmm", 4, false);
  EXPECT_EQ(applications(decompressor.getStats()), 1);
}

TEST_F(BuiltinInk, UncompressedSelectedFaceAndSharedAliasesUseTheSameInk) {
  auto raw = bitmap(plain, 'm');
  auto expected = raw;
  const auto* glyph = plain.getGlyph('m');
  inkBolden::apply(expected.data(), glyph->width, glyph->height, false, 5, false);
  ASSERT_NE(raw, expected);
  configure(manager, 8, 5, false);
  EXPECT_EQ(bitmap(plain, 'm'), expected);
  EXPECT_EQ(bitmap(plain, 'm'), expected);
  configure(manager, 7, 5, false);
  EXPECT_EQ(bitmap(plain, 'm'), raw);
  manager.prewarmCache(9, "m", 1, false);
  EXPECT_EQ(applications(decompressor.getStats()), 3);
}
