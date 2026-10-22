// FontOwnName::render on the real SdCardFont: a font whose file lacks only what the name does not need (U+FFFD,
// which every prewarm adds, a space, an 'x') is drawn; a name with a letter the file lacks is not.
#include <FontOwnName.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
void put16(size_t at, uint16_t value) {
  sdFontTestFile[at] = value;
  sdFontTestFile[at + 1] = value >> 8;
}
void put32(size_t at, uint32_t value) {
  put16(at, value);
  put16(at + 2, value >> 16);
}

// A one-style 2-bit .cpfont holding `codepoints`: each glyph 5 x 7 px (the x 5 x 4, a space nothing) with every pixel
// at the first grey level (1), advance 6 px, standing on the baseline (top 7). Header 32 bytes, style entry 32, then
// the intervals, the glyph table and the bitmaps.
void makeFont(const std::set<uint32_t>& codepoints) {
  std::vector<std::vector<uint32_t>> runs;
  for (const uint32_t cp : codepoints) {
    if (runs.empty() || runs.back().back() + 1 != cp) runs.emplace_back();
    runs.back().push_back(cp);
  }
  const size_t glyphs = codepoints.size(), glyphAt = 64 + runs.size() * 12;
  const auto widthOf = [](uint32_t cp) { return cp == ' ' ? 0 : 5; };
  const auto heightOf = [](uint32_t cp) { return cp == ' ' ? 0 : cp == 'x' ? 4 : 7; };
  size_t bitmapBytes = 0;
  for (const uint32_t cp : codepoints) bitmapBytes += (widthOf(cp) * heightOf(cp) + 3) / 4;
  sdFontTestFile.assign(glyphAt + glyphs * sizeof(EpdGlyph) + bitmapBytes, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  put16(10, 1);  // 2 bits a pixel
  sdFontTestFile[12] = 1;
  put32(36, static_cast<uint32_t>(runs.size()));
  put32(40, static_cast<uint32_t>(glyphs));
  sdFontTestFile[44] = 10;  // advanceY
  put16(45, 8);
  put32(56, 64);
  size_t offset = 0;
  for (size_t i = 0; i < runs.size(); ++i) {
    put32(64 + i * 12, runs[i].front());
    put32(68 + i * 12, runs[i].back());
    put32(72 + i * 12, static_cast<uint32_t>(offset));
    offset += runs[i].size();
  }
  size_t dataAt = 0, index = 0;
  const size_t bitmapAt = glyphAt + glyphs * sizeof(EpdGlyph);
  for (const uint32_t cp : codepoints) {
    EpdGlyph glyph{};
    glyph.width = static_cast<uint8_t>(widthOf(cp));
    glyph.height = static_cast<uint8_t>(heightOf(cp));
    glyph.advanceX = (cp == ' ' ? 3 : 6) << 4;
    glyph.top = static_cast<int16_t>(glyph.height);
    glyph.dataLength = static_cast<uint16_t>((glyph.width * glyph.height + 3) / 4);
    glyph.dataOffset = static_cast<uint32_t>(dataAt);
    std::memcpy(sdFontTestFile.data() + glyphAt + index++ * sizeof(glyph), &glyph, sizeof(glyph));
    std::memset(sdFontTestFile.data() + bitmapAt + dataAt, 0x55, glyph.dataLength);
    dataAt += glyph.dataLength;
  }
}

std::set<uint32_t> latin() {
  std::set<uint32_t> out = {' '};
  for (uint32_t cp = 'A'; cp <= 'z'; ++cp) out.insert(cp);
  return out;
}

bool inked(const fontownname::Picture& picture, int x, int y) {
  return picture.bits[y * ((picture.width + 7) / 8) + x / 8] & (0x80 >> (x % 8));
}

bool render(const char* name, fontownname::Picture& out, int maxWidth = 300, int maxHeight = 44) {
  return fontownname::render("/.fonts/Fake/Fake_12.cpfont", name, maxWidth, maxHeight, out);
}
}  // namespace

TEST(FontOwnName, NameIsDrawnWhenOnlyTheReplacementGlyphIsMissing) {
  // Geist and BeVietnamPro packs have no U+FFFD: prewarm, which always asks for it, counts one miss that is not a letter.
  auto cps = latin();
  makeFont(cps);
  fontownname::Picture picture;
  ASSERT_TRUE(render("Geist", picture));
  cps.insert(0xFFFD);
  makeFont(cps);
  fontownname::Picture withReplacement;
  ASSERT_TRUE(render("Geist", withReplacement));
  EXPECT_EQ(picture.width, withReplacement.width);
  EXPECT_EQ(picture.height, withReplacement.height);
}

TEST(FontOwnName, PictureIsTheNameInTheFilesGlyphs) {
  makeFont(latin());
  fontownname::Picture picture;
  ASSERT_TRUE(render("Be Vietnam Pro", picture));
  // 12 letters of 6 px and 2 spaces of 3: the ink runs from the first glyph's left edge to the last one's right edge.
  EXPECT_EQ(7, picture.height);
  EXPECT_EQ(7, picture.baseline);
  EXPECT_EQ(12 * 6 + 2 * 3 - 1, picture.width);
  EXPECT_EQ(4, picture.xHeight);
  EXPECT_EQ(static_cast<size_t>((picture.width + 7) / 8 * picture.height), picture.size);
  // The first grey level is ink (the store's pictures keep it too): the whole box of the first letter.
  for (int y = 0; y < 7; ++y)
    for (int x = 0; x < 5; ++x) EXPECT_TRUE(inked(picture, x, y)) << x << "," << y;
  EXPECT_FALSE(inked(picture, 5, 3));  // the gap before the next letter
  // The space is a gap of 3 px after the 2 letters "Be": letters at 0 and 6, the space's 3 px, the V at 15.
  EXPECT_FALSE(inked(picture, 13, 3));
  EXPECT_TRUE(inked(picture, 15, 3));
}

TEST(FontOwnName, SpaceAndXThatTheFileLacksAreNotReasonsToFail) {
  auto cps = latin();
  cps.erase(' ');
  cps.erase('x');
  makeFont(cps);
  fontownname::Picture picture;
  ASSERT_TRUE(render("Be Vietnam Pro", picture));  // no replacement glyph, no space, no x: three misses, none a letter
  EXPECT_EQ(0, picture.xHeight);
  EXPECT_EQ(11 * 6 + 2 * 2 + 5, picture.width);  // the missing spaces still open a gap, a quarter of the line high (2 px)
}

TEST(FontOwnName, LetterTheFileLacksFails) {
  makeFont(latin());
  fontownname::Picture picture;
  EXPECT_FALSE(render("Caf\xC3\xA9", picture));  // é is not in the file
  EXPECT_EQ(nullptr, picture.bits.get());
}

TEST(FontOwnName, NameTallerOrWiderThanTheRowIsCutOrRefused) {
  makeFont(latin());
  fontownname::Picture picture;
  EXPECT_FALSE(render("Geist", picture, 300, 6));  // 7 px high, the row has 6
  ASSERT_TRUE(render("Brygada Brygada Brygada", picture, 40, 44));  // cut at the label's width
  EXPECT_LE(picture.width, 40);
  EXPECT_GT(picture.width, 30);
}
