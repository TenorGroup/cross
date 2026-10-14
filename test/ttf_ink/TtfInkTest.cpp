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

TEST(TtfInk, NineStrengths) {
  constexpr int32_t expected[] = {-48, -32, -16, 0, 16, 32, 48, 64, 96};
  for (int level = 0; level < 9; ++level) EXPECT_EQ(readerInk::outlineStrength(level), expected[level]);
  EXPECT_EQ(readerInk::outlineStrength(-1), 0);
  EXPECT_EQ(readerInk::outlineStrength(9), 0);
}

TEST(TtfInk, ProductionFtAdvanceAtEveryLevelIsIdentical) {
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
      for (int stored = 0; stored < readerInk::LEVEL_COUNT; ++stored) {
        options.embolden26_6 = readerInk::outlineStrength(stored);
        ASSERT_TRUE(font.setRenderOptions(options));
        freeink::font::FtFont::GlyphMetrics level;
        ASSERT_TRUE(font.metrics26_6(cp, 33 * 64, level));
        EXPECT_EQ(raw.advance26_6, level.advance26_6) << cp << ' ' << weight << ' ' << italic << ' ' << stored;
        if (stored == 0 || stored == 8)
          std::printf("TTF cp=%u weight=%d italic=%d advance0=%d advance%s=%d\n", cp, weight, italic,
                      raw.advance26_6, stored ? "+5" : "-3", level.advance26_6);
      }
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

namespace {
struct Ink {
  long sum = 0;
  double x = 0, y = 0;  // ink-weighted centre, in pixels from the pen position, y up
};
Ink inkOf(TtfEpdFont& font, uint32_t cp, EpdFontFamily::Style style) {
  Ink ink;
  const auto* glyph = font.family().getGlyph(cp, style);
  const auto bytes = bitmap(font, cp, style);
  if (!glyph) return ink;
  for (int row = 0; row < glyph->height; ++row) {
    for (int col = 0; col < glyph->width; ++col) {
      const int index = row * glyph->width + col;
      const int value = (bytes[index >> 2] >> ((3 - (index & 3)) * 2)) & 3;
      ink.sum += value;
      ink.x += value * (glyph->left + col + 0.5);
      ink.y += value * (glyph->top - row - 0.5);
    }
  }
  if (ink.sum) ink.x /= ink.sum, ink.y /= ink.sum;
  return ink;
}
}  // namespace

// A thin level removes ink and keeps the stroke where it was: the outline is moved back by the half
// strength FT_Outline_Embolden shifts it, as the font pack bake does. The same holds above level 0.
TEST(TtfInk, EveryLevelKeepsTheStrokeCentreAndOrdersTheInk) {
  const auto bytes = source();
  for (const uint32_t cp : {'o', 'm', 'l', 'H'}) {
    // The round "o" shows the placement alone (about 0.04 px off); the flat-sided letters move up to 0.3 px
    // with the 2 bit quantising of their hinted edges. Without the move back the shift is half the strength:
    // 0.375 px at -3 and 0.75 px at +5.
    const double tolerance = cp == 'o' ? 0.15 : 0.35;
    TtfEpdFont font;
    font.addResidentSource(0, bytes.data(), bytes.size());
    ASSERT_TRUE(font.load(16));
    font.setInkStrength(0);
    const Ink base = inkOf(font, cp, EpdFontFamily::REGULAR);
    ASSERT_GT(base.sum, 0);
    long previous = 0;
    for (int stored = 0; stored < readerInk::LEVEL_COUNT; ++stored) {
      font.setInkStrength(readerInk::outlineStrength(stored));
      const Ink ink = inkOf(font, cp, EpdFontFamily::REGULAR);
      EXPECT_GT(ink.sum, previous) << char(cp) << " level " << stored - readerInk::ZERO;
      previous = ink.sum;
      EXPECT_NEAR(ink.x, base.x, tolerance) << char(cp) << " level " << stored - readerInk::ZERO;
      EXPECT_NEAR(ink.y, base.y, tolerance) << char(cp) << " level " << stored - readerInk::ZERO;
    }
  }
}

TEST(TtfInk, ThinLevelLoadedDirectlyMatchesTheOneSetLater) {
  const auto bytes = source();
  for (const int stored : {0, 1, 2}) {
    TtfEpdFont set;
    set.addResidentSource(0, bytes.data(), bytes.size());
    ASSERT_TRUE(set.load(16));
    set.setInkStrength(readerInk::outlineStrength(stored));
    TtfEpdFont direct;
    direct.addResidentSource(0, bytes.data(), bytes.size());
    ASSERT_TRUE(direct.load(16, true, 32768, 768, readerInk::outlineStrength(stored)));
    const auto expected = bitmap(set, 'm', EpdFontFamily::REGULAR);
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(bitmap(direct, 'm', EpdFontFamily::REGULAR), expected);
  }
}
