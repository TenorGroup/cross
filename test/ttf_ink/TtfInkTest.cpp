#include <FtFont.h>
#include <TtfEpdFont.h>
#include <ReaderInkWeight.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace {
std::vector<uint8_t> source() {
  std::ifstream input(TEST_REPO_ROOT "/lib/EpdFont/builtinFonts/source/Geist/Geist-Regular.ttf", std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
template <typename Font>
void configure(Font& font, int32_t strength) {
  if constexpr (requires { font.setInkStrength(strength); }) font.setInkStrength(strength);
}
template <typename Font>
void configureAa(Font& font, bool aa) {
  if constexpr (requires { font.setInkStrength(160, aa); }) font.setInkStrength(160, aa);
}
std::vector<uint8_t> bitmap(TtfEpdFont& font, uint32_t cp, EpdFontFamily::Style style) {
  auto family = font.family();
  const auto* glyph = family.getGlyph(cp, style);
  if (!glyph || !glyph->dataLength) return {};
  const auto* data = family.getData(style);
  const auto* bytes = data->vectorBitmapHandler(data->glyphMissCtx, glyph);
  return {bytes, bytes + glyph->dataLength};
}
}

TEST(TtfInk, SixStrengths) {
  constexpr int32_t expected[] = {0, 16, 32, 48, 64, 96};
  for (int level = 0; level < 6; ++level) EXPECT_EQ(readerInk::outlineStrength(level), expected[level]);
  EXPECT_EQ(readerInk::outlineStrength(-1), 0);
  EXPECT_EQ(readerInk::outlineStrength(6), 0);
}

TEST(TtfInk, ProductionFtAdvanceAtZeroAndFiveIsIdentical) {
  const auto bytes = source();
  ASSERT_FALSE(bytes.empty());
  for (int weight : {400, 700}) for (bool italic : {false, true}) {
    freeink::font::FtFont font;
    ASSERT_TRUE(font.init(bytes.data(), bytes.size(), 33, weight, italic));
    for (uint32_t cp : {'m', 'l', 'A', 'W', 'j', ' ', '\x01'}) {
      if (!font.hasGlyph(cp)) continue;
      freeink::font::FtFont::RenderOptions options;
      options.hinting = freeink::font::FtFont::HintingMode::Auto;
      options.stemDarkening = true;
      ASSERT_TRUE(font.setRenderOptions(options));
      freeink::font::FtFont::GlyphMetrics raw;
      ASSERT_TRUE(font.metrics26_6(cp, 33 * 64, raw));
      options.embolden26_6 = readerInk::outlineStrength(5);
      ASSERT_TRUE(font.setRenderOptions(options));
      freeink::font::FtFont::GlyphMetrics bold;
      ASSERT_TRUE(font.metrics26_6(cp, 33 * 64, bold));
      EXPECT_EQ(raw.advance26_6, bold.advance26_6) << cp << ' ' << weight << ' ' << italic;
      std::printf("TTF cp=%u weight=%d italic=%d advance0=%d advance5=%d\n", cp, weight, italic,
                  raw.advance26_6, bold.advance26_6);
    }
  }
}

TEST(TtfInk, StrengthChangeFlushesAllFourFacesWithoutBitmapBoldening) {
  const auto bytes = source();
  TtfEpdFont font;
  font.addResidentSource(0, bytes.data(), bytes.size());
  ASSERT_TRUE(font.load(16));
  for (uint8_t style = 0; style < 4; ++style) {
    const auto role = static_cast<EpdFontFamily::Style>(style);
    configure(font, 0);
    const auto raw = bitmap(font, 'm', role);
    const auto advance = font.family().getGlyph('m', role)->advanceX;
    ASSERT_FALSE(raw.empty());
    configure(font, 160);
    const auto bold = bitmap(font, 'm', role);
    EXPECT_NE(raw, bold);
    EXPECT_EQ(font.family().getGlyph('m', role)->advanceX, advance);
    configure(font, 160);
    EXPECT_EQ(bitmap(font, 'm', role), bold);
    TtfEpdFont direct;
    direct.addResidentSource(0, bytes.data(), bytes.size());
    ASSERT_TRUE(direct.load(16, true, 32768, 768, 160));
    EXPECT_EQ(bitmap(direct, 'm', role), bold);
    configure(font, 0);
    EXPECT_EQ(bitmap(font, 'm', role), raw);
  }
}

TEST(TtfInk, AntiAliasingChangeFlushesRasterWhileSameConfigRetainsIt) {
  std::printf("TtfEpdFont_size=%zu\n", sizeof(TtfEpdFont));
  const auto bytes = source();
  TtfEpdFont font;
  font.addResidentSource(0, bytes.data(), bytes.size());
  ASSERT_TRUE(font.load(16, true, 32768, 768, 160));
  const auto expected = bitmap(font, 'm', EpdFontFamily::REGULAR);
  const auto family = font.family();
  const auto* glyph = family.getGlyph('m');
  const auto* data = family.getData();
  auto* raster = const_cast<uint8_t*>(data->vectorBitmapHandler(data->glyphMissCtx, glyph));
  raster[0] ^= 255;
  configureAa(font, true);
  EXPECT_NE(bitmap(font, 'm', EpdFontFamily::REGULAR), expected);
  configureAa(font, false);
  EXPECT_EQ(bitmap(font, 'm', EpdFontFamily::REGULAR), expected);
}
