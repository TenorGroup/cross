#include <HalStorage.h>
#include <SdCardFont.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

namespace {
size_t failNextArraySize = 0;
size_t nothrowArrayAllocations = 0;
}  // namespace

// Keep array allocation and deletion paired, including arrays allocated by the test framework.
void* operator new[](size_t size) {
  void* allocation = std::malloc(size == 0 ? 1 : size);
  if (!allocation) {
    std::fputs("Unexpected host OOM in SdCardFontTest\n", stderr);
    std::exit(EXIT_FAILURE);
  }
  return allocation;
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  ++nothrowArrayAllocations;
  if (failNextArraySize != 0 && size == failNextArraySize) {
    failNextArraySize = 0;
    return nullptr;
  }
  return std::malloc(size == 0 ? 1 : size);
}

void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }

namespace {
constexpr uint32_t FIRST = 0xAC00;
constexpr uint32_t GLYPHS = 513;
constexpr uint16_t BITMAP_BYTES = 128;

void put16(size_t at, uint16_t value) {
  sdFontTestFile[at] = value;
  sdFontTestFile[at + 1] = value >> 8;
}
void put32(size_t at, uint32_t value) {
  put16(at, value);
  put16(at + 2, value >> 16);
}

// `glyphs` Hangul glyphs from FIRST, the last of them U+FFFD.
void makeFont(uint32_t glyphs = GLYPHS) {
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  const size_t BITMAP_OFFSET = GLYPH_OFFSET + glyphs * sizeof(EpdGlyph);
  sdFontTestFile.assign(BITMAP_OFFSET + glyphs * BITMAP_BYTES, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = 1;
  put32(36, 2);
  put32(40, glyphs);
  sdFontTestFile[44] = 32;
  put16(45, 32);
  put32(56, 64);
  put32(64, FIRST);
  put32(68, FIRST + glyphs - 2);
  put32(76, 0xFFFD);
  put32(80, 0xFFFD);
  put32(84, glyphs - 1);
  for (uint32_t i = 0; i < glyphs; ++i) {
    EpdGlyph glyph{};
    glyph.width = 32;
    glyph.height = 32;
    glyph.advanceX = 32 << 4;
    glyph.top = 32;
    glyph.dataLength = BITMAP_BYTES;
    // Store bitmaps in reverse glyph order to exercise sorted reads on rebuild.
    glyph.dataOffset = (glyphs - 1 - i) * BITMAP_BYTES;
    std::memcpy(sdFontTestFile.data() + GLYPH_OFFSET + i * sizeof(glyph), &glyph, sizeof(glyph));
    std::memset(sdFontTestFile.data() + BITMAP_OFFSET + glyph.dataOffset, i % 251, BITMAP_BYTES);
  }
}

// Six Latin glyphs 'A'..'F' plus U+FFFD, with left classes for 'A' and 'C',
// right classes for 'B' and 'D', and a class matrix whose top-left 2×2 corner
// holds their pairs. `extraEntries` appends entries for U+0100 onwards to both
// class tables, so the tables span several read blocks. They use `extraClass`,
// or classes 3..classCount in turn when it is 0. A `classCount` of 0 writes no
// kern data. `ligature` adds one pair, E+F -> A.
void makeKerningFont(uint16_t extraEntries = 0, uint8_t classCount = 2, uint8_t extraClass = 2, bool ligature = false) {
  constexpr uint32_t KERN_GLYPHS = 7;
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  constexpr size_t KERN_OFFSET = GLYPH_OFFSET + KERN_GLYPHS * sizeof(EpdGlyph);
  constexpr uint8_t LEFT[][2] = {{'A', 1}, {'C', 2}};
  constexpr uint8_t RIGHT[][2] = {{'B', 1}, {'D', 2}};
  constexpr int8_t MATRIX[2][2] = {{-3, 0}, {4, -5}};
  const uint16_t entries = classCount ? 2 + extraEntries : 0;
  const size_t matrixBytes = static_cast<size_t>(classCount) * classCount;
  const size_t ligatureOffset = KERN_OFFSET + entries * 3 * 2 + matrixBytes;
  const size_t bitmapOffset = ligatureOffset + (ligature ? 8 : 0);
  sdFontTestFile.assign(bitmapOffset + KERN_GLYPHS * BITMAP_BYTES, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = 1;
  put32(36, 2);
  put32(40, KERN_GLYPHS);
  sdFontTestFile[44] = 32;
  put16(45, 32);
  put16(49, entries);  // left class entries
  put16(51, entries);  // right class entries
  sdFontTestFile[53] = classCount;
  sdFontTestFile[54] = classCount;
  sdFontTestFile[55] = ligature ? 1 : 0;
  put32(56, 64);
  put32(64, 'A');
  put32(68, 'F');
  put32(76, 0xFFFD);
  put32(80, 0xFFFD);
  put32(84, KERN_GLYPHS - 1);
  for (uint32_t i = 0; i < KERN_GLYPHS; ++i) {
    EpdGlyph glyph{};
    glyph.width = 32;
    glyph.height = 32;
    glyph.advanceX = 32 << 4;
    glyph.top = 32;
    glyph.dataLength = BITMAP_BYTES;
    glyph.dataOffset = i * BITMAP_BYTES;
    std::memcpy(sdFontTestFile.data() + GLYPH_OFFSET + i * sizeof(glyph), &glyph, sizeof(glyph));
  }
  size_t at = KERN_OFFSET;
  for (const auto* table : {LEFT, RIGHT}) {
    if (classCount == 0) break;
    for (size_t i = 0; i < 2; ++i, at += 3) {
      put16(at, table[i][0]);
      sdFontTestFile[at + 2] = table[i][1];
    }
    for (uint16_t i = 0; i < extraEntries; ++i, at += 3) {
      put16(at, 0x100 + i);
      sdFontTestFile[at + 2] = extraClass ? extraClass : 3 + i % (classCount - 2);
    }
  }
  for (size_t row = 0; classCount > 0 && row < 2; ++row) {
    std::memcpy(sdFontTestFile.data() + at + row * classCount, MATRIX[row], sizeof(MATRIX[row]));
  }
  if (ligature) {
    put32(ligatureOffset, 'E' << 16 | 'F');
    put32(ligatureOffset + 4, 'A');
  }
}

std::string latinPage(const char* ascii, uint32_t first, uint32_t count) {
  std::string text = ascii;
  for (uint32_t cp = first; cp < first + count; ++cp) {
    text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

std::string page(uint32_t first, uint32_t count) {
  std::string text;
  text.reserve(count * 3);
  for (uint32_t cp = first; cp < first + count; ++cp) {
    text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

uint32_t residentCount(SdCardFont& font) {
  const auto* data = font.getEpdFont()->data;
  uint32_t count = 0;
  for (uint32_t i = 0; i < data->intervalCount; ++i) {
    count += data->intervals[i].last - data->intervals[i].first + 1;
  }
  return count;
}

void expectPageBitmaps(SdCardFont& font, uint32_t first, uint32_t count) {
  const auto* data = font.getEpdFont()->data;
  for (uint32_t cp = first; cp < first + count; ++cp) {
    const EpdGlyph* glyph = nullptr;
    for (uint32_t i = 0; i < data->intervalCount; ++i) {
      const auto& interval = data->intervals[i];
      if (cp >= interval.first && cp <= interval.last) glyph = data->glyph + interval.offset + cp - interval.first;
    }
    ASSERT_NE(nullptr, glyph) << cp;
    ASSERT_EQ(BITMAP_BYTES, glyph->dataLength);
    for (uint16_t i = 0; i < BITMAP_BYTES; ++i) {
      ASSERT_EQ((cp - FIRST) % 251, data->bitmap[glyph->dataOffset + i]);
    }
  }
}
}  // namespace

namespace {
template <typename Font>
void configureInk(Font& font, uint8_t level, bool aa) {
  if constexpr (requires { font.setReaderInk(level, aa); }) font.setReaderInk(level, aa);
}
std::vector<uint8_t> inkBitmap(SdCardFont& font, uint32_t codepoint) {
  const auto* face = font.getEpdFont();
  const auto* glyph = face->getGlyph(codepoint);
  const auto* bytes = font.isOverflowGlyph(glyph) ? font.getOverflowBitmap(glyph) : face->data->bitmap + glyph->dataOffset;
  return {bytes, bytes + glyph->dataLength};
}
void makeInkFont(bool twoBit) {
  makeFont();
  put16(10, twoBit ? 1 : 0);
  if (twoBit) for (uint32_t index = 0; index < GLYPHS; ++index) {
    EpdGlyph glyph;
    const auto offset = 88 + index * sizeof(glyph);
    std::memcpy(&glyph, sdFontTestFile.data() + offset, sizeof(glyph));
    glyph.height = 16;
    glyph.top = 16;
    std::memcpy(sdFontTestFile.data() + offset, &glyph, sizeof(glyph));
  }
}
}

TEST(SdCardFontTest, InkMiniAndOverflowMatchOriginalWithoutAccumulation) {
  std::printf("SdCardFont_size=%zu\n", sizeof(SdCardFont));
  for (bool twoBit : {false, true}) {
    makeInkFont(twoBit);
    SdCardFont font;
    ASSERT_TRUE(font.load("fixture"));
    const uint32_t codepoint = FIRST + 17;
    const auto text = page(codepoint, 1);
    ASSERT_EQ(0, font.prewarm(text.c_str(), 15, false, false, false));
    const auto original = *font.getEpdFont()->getGlyph(codepoint);
    const auto raw = inkBitmap(font, codepoint);
    for (int level = 0; level < 6; ++level) for (bool aa : {false, true}) {
      auto expected = raw;
      configureInk(font, level, aa);
      ASSERT_EQ(0, font.prewarm(text.c_str(), 15, false, false, false));
      EXPECT_EQ(inkBitmap(font, codepoint), expected) << level << ' ' << aa << ' ' << twoBit;
      const auto* glyph = font.getEpdFont()->getGlyph(codepoint);
      EXPECT_EQ(glyph->width, original.width);
      EXPECT_EQ(glyph->height, original.height);
      EXPECT_EQ(glyph->advanceX, original.advanceX);
      EXPECT_EQ(glyph->top, original.top);
      EXPECT_EQ(glyph->left, original.left);
      font.releaseResidentCaches();
      EXPECT_EQ(inkBitmap(font, codepoint), expected);
      const auto reads = sdFontTestReads;
      EXPECT_EQ(inkBitmap(font, codepoint), expected);
      EXPECT_EQ(sdFontTestReads, reads);
      for (uint32_t index = 1; index <= 10; ++index) inkBitmap(font, codepoint + index);
      EXPECT_EQ(inkBitmap(font, codepoint), expected);
    }
  }
}

TEST(SdCardFontTest, InkSettingLeavesRetainedMiniAndAdvancesUnchanged) {
  makeInkFont(true);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST + 17, 1);
  ASSERT_EQ(font.buildAdvanceTable(text.c_str(), 1), 0);
  const auto advance = font.getAdvance(FIRST + 17, 0);
  ASSERT_NE(advance, 0);
  configureInk(font, 1, true);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  const auto light = inkBitmap(font, FIRST + 17);
  font.clearCache();
  const auto before = sdFontTestReads;
  configureInk(font, 1, true);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  EXPECT_EQ(sdFontTestReads, before);
  configureInk(font, 5, true);
  EXPECT_EQ(font.getAdvance(FIRST + 17, 0), advance);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  EXPECT_EQ(sdFontTestReads, before);
  EXPECT_EQ(inkBitmap(font, FIRST + 17), light);
  configureInk(font, 1, true);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  EXPECT_EQ(inkBitmap(font, FIRST + 17), light);
  font.clearCache();
  const auto beforeAa = sdFontTestReads;
  configureInk(font, 1, false);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  EXPECT_EQ(sdFontTestReads, beforeAa);
}

TEST(SdCardFontTest, InkMetadataOnlyThenFullPrewarmUsesOriginalBitmap) {
  makeInkFont(true);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST + 17, 1);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  auto expected = inkBitmap(font, FIRST + 17);
  const auto glyph = *font.getEpdFont()->getGlyph(FIRST + 17);
  font.releaseResidentCaches();
  configureInk(font, 5, true);
  ASSERT_EQ(font.prewarm(text.c_str(), 1, true, false, false), 0);
  const auto reads = sdFontTestReads;
  ASSERT_EQ(font.prewarm(text.c_str(), 1, false, false, false), 0);
  EXPECT_GT(sdFontTestReads, reads);
  EXPECT_EQ(inkBitmap(font, FIRST + 17), expected);
}

TEST(SdCardFontTest, LayoutIdentityIgnoresRasterOffsetsAndTracksAdvances) {
  makeFont();
  SdCardFont original;
  ASSERT_TRUE(original.load("fixture"));
  const auto identity = original.contentHash();
  auto payload = sdFontTestFile;
  sdFontTestFile.insert(sdFontTestFile.begin() + 64, 32, 0);
  put32(56, 96);
  SdCardFont relocated;
  ASSERT_TRUE(relocated.load("fixture"));
  EXPECT_EQ(relocated.contentHash(), identity);
  sdFontTestFile = payload;
  put16(88 + 2, 513);
  SdCardFont differentAdvance;
  ASSERT_TRUE(differentAdvance.load("fixture"));
  EXPECT_NE(differentAdvance.contentHash(), identity);
  sdFontTestFile = payload;
  put16(45, 31);
  SdCardFont differentMetrics;
  ASSERT_TRUE(differentMetrics.load("fixture"));
  EXPECT_NE(differentMetrics.contentHash(), identity);
}

TEST(SdCardFontTest, LayoutIdentityTracksKerningAndLigatureTables) {
  makeKerningFont(0, 2, 2, true);
  SdCardFont original;
  ASSERT_TRUE(original.load("fixture"));
  const auto identity = original.contentHash();
  const auto payload = sdFontTestFile;
  const size_t kernStart = 88 + 7 * sizeof(EpdGlyph);
  sdFontTestFile[kernStart + 12] ^= 1;
  SdCardFont differentKerning;
  ASSERT_TRUE(differentKerning.load("fixture"));
  EXPECT_NE(differentKerning.contentHash(), identity);
  sdFontTestFile = payload;
  sdFontTestFile[kernStart + 16 + 6] ^= 1;
  SdCardFont differentLigature;
  ASSERT_TRUE(differentLigature.load("fixture"));
  EXPECT_NE(differentLigature.contentHash(), identity);
}

TEST(SdCardFontTest, AdvanceMetadataReadSkipsBitmap) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));

  sdFontTestReads = 0;
  uint16_t advance = 0;
  ASSERT_TRUE(font.readAdvanceMetadata(FIRST + 17, 0, advance));
  EXPECT_EQ(32U << 4, advance);
  EXPECT_EQ(1U, sdFontTestReads);

  sdFontTestReads = 0;
  ASSERT_NE(nullptr, font.getEpdFont()->getGlyph(FIRST + 17));
  EXPECT_EQ(2U, sdFontTestReads);
}

TEST(SdCardFontTest, AdvanceMetadataPreservesZeroAdvance) {
  makeFont();
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  put16(GLYPH_OFFSET + 17 * sizeof(EpdGlyph) + 2, 0);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));

  sdFontTestReads = 0;
  uint16_t advance = 123;
  EXPECT_TRUE(font.readAdvanceMetadata(FIRST + 17, 0, advance));
  EXPECT_EQ(0U, advance);
  EXPECT_EQ(1U, sdFontTestReads);
}

TEST(SdCardFontTest, ResidentSubsetSkipsCodepointBuffer) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST + 17, 1);
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  const auto expected = inkBitmap(font, FIRST + 17);

  sdFontTestReads = 0;
  failNextArraySize = 512 * sizeof(uint32_t);
  EXPECT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  EXPECT_EQ(512 * sizeof(uint32_t), failNextArraySize);
  EXPECT_EQ(0U, sdFontTestReads);
  EXPECT_EQ(expected, inkBitmap(font, FIRST + 17));
  failNextArraySize = 0;
}

TEST(SdCardFontTest, CompletePagesReplaceEarlierGlyphs) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (uint32_t offset : {0U, 100U, 200U}) {
    const uint32_t first = FIRST + offset;
    const auto text = page(first, 100);
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    EXPECT_EQ(101U, residentCount(font));  // includes replacement glyph
    expectPageBitmaps(font, first, 100);
  }
}

TEST(SdCardFontTest, IncrementalUiStringsStillAccumulate) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 5).c_str(), 1));
  ASSERT_EQ(0, font.prewarm(page(FIRST + 5, 5).c_str(), 1));
  EXPECT_EQ(11U, residentCount(font));
  expectPageBitmaps(font, FIRST, 10);
}

TEST(SdCardFontTest, UnderusedBuffersKeepTheFreshlyPrewarmedPageUntilItIsDrawn) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 200).c_str(), 1, false, false, false));
  font.clearCache();
  for (uint32_t offset : {300U, 400U, 200U, 0U}) {
    const auto text = page(FIRST + offset, 100);
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    font.clearCache();  // idle prewarm scope closes
    font.clearCache();  // actual page render scope opens
    sdFontTestReads = 0;
    ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
    EXPECT_EQ(0U, sdFontTestReads);
    expectPageBitmaps(font, FIRST + offset, 100);
  }
}

TEST(SdCardFontTest, BitmapGrowthPreservesReadOrderAndAllGlyphData) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (uint32_t count : {50U, 100U, 200U, 400U}) {
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(page(FIRST, count).c_str(), 1, false, false, false));
    EXPECT_EQ(count + 1, residentCount(font));
    expectPageBitmaps(font, FIRST, count);
  }
}

TEST(SdCardFontTest, FragmentedBitmapGrowthRebuildsMetadataAndKeepsThePrefetch) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 50).c_str(), 1, false, false, false));
  struct HeapReportGuard {
    ~HeapReportGuard() { ESP.largestBlock = 200 * 1024; }
  } guard;
  ESP.largestBlock = 8 * 1024;
  const auto text = page(FIRST, 200);
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  expectPageBitmaps(font, FIRST, 200);
  font.clearCache();
  sdFontTestReads = 0;
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  EXPECT_EQ(0U, sdFontTestReads);
}

TEST(SdCardFontTest, BitmapAllocationRetriesAfterEvictingRebuildableAdvances) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const auto text = page(FIRST, 200);
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  ASSERT_TRUE(font.hasAdvanceTable());
  ASSERT_EQ(0, font.prewarm(page(FIRST, 50).c_str(), 1, false, false, false));
  struct AllocationGuard {
    ~AllocationGuard() {
      ESP.largestBlock = 200 * 1024;
      failNextArraySize = 0;
    }
  } guard;
  ESP.largestBlock = 8 * 1024;
  failNextArraySize = 201 * BITMAP_BYTES;
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1, false, false, false));
  EXPECT_EQ(0U, failNextArraySize);
  EXPECT_FALSE(font.hasAdvanceTable());
  expectPageBitmaps(font, FIRST, 200);

  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  for (uint32_t cp = FIRST; cp < FIRST + 200; ++cp) {
    EXPECT_EQ(32 << 4, font.getAdvance(cp, 0));
  }
}

TEST(SdCardFontTest, PagesKernWithTheFontsClassMatrix) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true, false));
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(0, epd->getKerning('A', 'D'));
  EXPECT_EQ(4, epd->getKerning('C', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
  EXPECT_EQ(0, epd->getKerning('B', 'D'));
  EXPECT_EQ(0, epd->getKerning('E', 'B'));
}

TEST(SdCardFontTest, KernRequestsServedFromAKernFreeMiniStillKern) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, false, false));  // kern-free prewarm, e.g. a UI string
  struct Step {
    const char* text;
    uint32_t left, right;
    int8_t kern;
  };
  // A subset without kerning pairs, then subsets whose pairs the earlier ones did not cover.
  for (const Step& step : {Step{"EF", 'E', 'F', 0}, Step{"AB", 'A', 'B', -3}, Step{"CD", 'C', 'D', -5}}) {
    ASSERT_EQ(0, font.prewarm(step.text, 1, false, true, false));
    EXPECT_EQ(step.kern, font.getEpdFont()->getKerning(step.left, step.right)) << step.text;
  }
}

TEST(SdCardFontTest, RedrawsAfterAKernFreeRebuildStillKern) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("ABCD", 1, false, true, false));
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, false, false));  // kern-free rebuild, e.g. a UI string
  ASSERT_EQ(0, font.prewarm("ABCD", 1, false, true, false));     // the page again, served from that cache
  EXPECT_EQ(-3, font.getEpdFont()->getKerning('A', 'B'));
  EXPECT_EQ(-5, font.getEpdFont()->getKerning('C', 'D'));
}

TEST(SdCardFontTest, ClassIdsPastTheMatrixAreUnkerned) {
  makeKerningFont(1, 2, 250);  // U+0100 claims class 250 in a 2×2 matrix
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(1, font.prewarm(latinPage("ABCD", 0x100, 1).c_str(), 1, false, true, false));  // U+0100 has no glyph
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(0, epd->getKerning('A', 0x100));
  EXPECT_EQ(0, epd->getKerning(0x100, 'B'));
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
}

TEST(SdCardFontTest, PagesCanUseAll255KernClasses) {
  makeKerningFont(253, 255, 0);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(253, font.prewarm(latinPage("ABCD", 0x100, 253).c_str(), 1, false, true, false));
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
  EXPECT_EQ(0, epd->getKerning(0x100, 0x1FC));
}

TEST(SdCardFontTest, AFailedKernBuildKeepsTheLigatures) {
  makeKerningFont(253, 255, 0, true);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  failNextArraySize = 255 * 255;  // the mini kern matrix
  ASSERT_EQ(253, font.prewarm(latinPage("ABCDEF", 0x100, 253).c_str(), 1, false, true, false));
  EXPECT_EQ(0U, failNextArraySize);
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(static_cast<uint32_t>('A'), epd->getLigature('E', 'F'));
  EXPECT_EQ(0, epd->getKerning('A', 'B'));
}

TEST(SdCardFontTest, LigatureRequestsServedFromAKernFreeMiniGetLigatures) {
  makeKerningFont(0, 0, 2, true);  // ligatures, no kern classes
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm("AEF", 1, false, false, false));  // kern-free prewarm, e.g. a UI string
  ASSERT_EQ(0, font.prewarm("EF", 1, false, true, false));
  EXPECT_EQ(static_cast<uint32_t>('A'), font.getEpdFont()->getLigature('E', 'F'));
}

TEST(SdCardFontTest, RedrawingAPrewarmedPageReadsNothing) {
  makeKerningFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  for (const char* text : {"ABCD", "DEF"}) {  // with and without kerning pairs
    font.clearCache();
    ASSERT_EQ(0, font.prewarm(text, 1, false, true, false));
    font.clearCache();
    sdFontTestReads = 0;
    ASSERT_EQ(0, font.prewarm(text, 1, false, true, false));
    EXPECT_EQ(0U, sdFontTestReads) << text;
  }
}

TEST(SdCardFontTest, LaterPagesReadOnlyTheKernClassBlocksTheyUse) {
  makeKerningFont(300);  // five 64-entry blocks per class table
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  sdFontTestReads = 0;
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true, false));
  const size_t firstReads = sdFontTestReads;
  EXPECT_EQ(-5, font.getEpdFont()->getKerning('C', 'D'));

  font.releaseResidentCaches();
  sdFontTestReads = 0;
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1, false, true, false));
  EXPECT_EQ(firstReads - 8, sdFontTestReads);  // 4 of the 5 blocks skipped in each table
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(4, epd->getKerning('C', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
}

TEST(SdCardFontTest, AdvancesStayCorrectWhenPagesAddCodepointsOutOfOrder) {
  makeFont();
  for (uint32_t i = 0; i < GLYPHS; ++i) {
    put16(64 + 24 + i * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (20 + i % 13) << 4);
  }
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  std::vector<uint32_t> added;
  for (uint32_t block : {3U, 0U, 5U, 1U, 4U, 2U}) {  // each merge lands before, between or after earlier ones
    ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST + block * 80, 80).c_str(), 1));
    for (uint32_t cp = FIRST + block * 80; cp < FIRST + block * 80 + 80; ++cp) added.push_back(cp);
    for (uint32_t cp : added) {
      ASSERT_EQ((20 + (cp - FIRST) % 13) << 4, font.getAdvance(cp, 0)) << std::hex << cp;
    }
  }
}

TEST(SdCardFontTest, AdvanceMergesPastTheCapKeepTheLowestCodepoints) {
  constexpr uint32_t CACHE_LIMIT = 768;  // SdCardFont::ADVANCE_CACHE_LIMIT
  constexpr uint32_t GLYPH_COUNT = 1001;
  makeFont(GLYPH_COUNT);
  for (uint32_t i = 0; i < GLYPH_COUNT; ++i) {
    put16(64 + 24 + i * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (20 + i % 13) << 4);
  }
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  // Reference: the sorted union of every page, truncated to the cap; a full table takes nothing more.
  std::vector<uint32_t> expected;
  for (uint32_t block : {6U, 1U, 9U, 3U, 0U, 7U, 4U, 8U, 2U, 5U}) {
    ASSERT_GE(font.buildAdvanceTable(page(FIRST + block * 100, 100).c_str(), 1), 0);
    if (expected.size() < CACHE_LIMIT) {
      for (uint32_t cp = FIRST + block * 100; cp < FIRST + block * 100 + 100; ++cp) expected.push_back(cp);
      std::sort(expected.begin(), expected.end());
      if (expected.size() > CACHE_LIMIT) expected.resize(CACHE_LIMIT);
    }
    for (uint32_t cp = FIRST; cp < FIRST + 1000; ++cp) {
      const bool cached = std::binary_search(expected.begin(), expected.end(), cp);
      ASSERT_EQ(cached ? (20 + (cp - FIRST) % 13) << 4 : 0, font.getAdvance(cp, 0)) << block << " " << std::hex << cp;
    }
  }
}

TEST(SdCardFontTest, AnEmptyOrFailedAdvanceBuildLeavesNoTable) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  font.buildAdvanceTable("", 1);
  EXPECT_FALSE(font.hasAdvanceTable());
  failNextArraySize = (768 + 2) * sizeof(uint32_t);  // the codepoint scratch
  const std::string largeText = page(FIRST, 10) + std::string(4096, ' ');
  EXPECT_EQ(-1, font.buildAdvanceTable(largeText.c_str(), 1));
  EXPECT_FALSE(font.hasAdvanceTable());
  ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST, 10).c_str(), 1));
  EXPECT_TRUE(font.hasAdvanceTable());
}

// --- Light load: a font's name in its own face ---

namespace {
// A four-style 1-bit .cpfont of U+0020..U+017F, the size of a real Latin pack's tables: per style a glyph table, kern
// class tables with a matrix, a ligature pair and 8-byte bitmaps that differ by style and glyph. Header 32 bytes, four
// style entries of 32, then each style's intervals, glyphs, kern left, kern right, matrix, ligatures, bitmaps.
constexpr uint32_t MULTI_FIRST = 0x20, MULTI_GLYPHS = 0x180 - 0x20, MULTI_KERN = 150;
constexpr uint8_t MULTI_CLASSES = 12;
constexpr size_t MULTI_BITMAP = 8;

uint8_t multiBitmapByte(uint8_t style, uint32_t index, size_t at) { return static_cast<uint8_t>(style * 61 + index * 7 + at); }

void makeMultiStyleFont() {
  const size_t kernBytes = MULTI_KERN * sizeof(EpdKernClassEntry), matrixBytes = MULTI_CLASSES * MULTI_CLASSES;
  const size_t styleBytes = 12 + MULTI_GLYPHS * sizeof(EpdGlyph) + 2 * kernBytes + matrixBytes + sizeof(EpdLigaturePair) +
                            MULTI_GLYPHS * MULTI_BITMAP;
  sdFontTestFile.assign(32 + 4 * 32 + 4 * styleBytes, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = 4;
  for (uint8_t style = 0; style < 4; ++style) {
    const size_t toc = 32 + style * 32, base = 32 + 4 * 32 + style * styleBytes;
    sdFontTestFile[toc] = style;
    put32(toc + 4, 1);
    put32(toc + 8, MULTI_GLYPHS);
    sdFontTestFile[toc + 12] = 20 + style;
    put16(toc + 13, 16);
    put16(toc + 15, static_cast<uint16_t>(-4));
    put16(toc + 17, MULTI_KERN);
    put16(toc + 19, MULTI_KERN);
    sdFontTestFile[toc + 21] = sdFontTestFile[toc + 22] = MULTI_CLASSES;
    sdFontTestFile[toc + 23] = 1;
    put32(toc + 24, static_cast<uint32_t>(base));
    put16(toc + 28, 15);
    put32(base, MULTI_FIRST);
    put32(base + 4, MULTI_FIRST + MULTI_GLYPHS - 1);
    put32(base + 8, 0);
    const size_t glyphAt = base + 12, kernAt = glyphAt + MULTI_GLYPHS * sizeof(EpdGlyph);
    const size_t matrixAt = kernAt + 2 * kernBytes, ligAt = matrixAt + matrixBytes, bitmapAt = ligAt + sizeof(EpdLigaturePair);
    for (uint32_t i = 0; i < MULTI_GLYPHS; ++i) {
      EpdGlyph glyph{};
      glyph.width = 8;
      glyph.height = MULTI_FIRST + i == 'x' ? 5 : 8;
      glyph.advanceX = static_cast<uint16_t>((6 + i % 5) << 4);
      glyph.top = glyph.height;
      glyph.dataLength = MULTI_BITMAP;
      glyph.dataOffset = i * MULTI_BITMAP;
      std::memcpy(sdFontTestFile.data() + glyphAt + i * sizeof(glyph), &glyph, sizeof(glyph));
      for (size_t at = 0; at < MULTI_BITMAP; ++at) sdFontTestFile[bitmapAt + i * MULTI_BITMAP + at] = multiBitmapByte(style, i, at);
    }
    for (uint32_t side = 0; side < 2; ++side)
      for (uint32_t i = 0; i < MULTI_KERN; ++i) {
        put16(kernAt + side * kernBytes + i * 3, static_cast<uint16_t>(MULTI_FIRST + i));
        sdFontTestFile[kernAt + side * kernBytes + i * 3 + 2] = static_cast<uint8_t>(1 + (i + side + style) % MULTI_CLASSES);
      }
    for (size_t i = 0; i < matrixBytes; ++i) sdFontTestFile[matrixAt + i] = static_cast<uint8_t>(i * 3 + style);
    put32(ligAt, 'f' << 16 | 'i');
    put32(ligAt + 4, 0xFB01);
  }
}

// What a drawing needs of a prewarmed regular (or other) style: the glyph record of each codepoint and its bitmap.
std::vector<uint8_t> prewarmed(SdCardFont& font, uint8_t style, const char* text) {
  std::vector<uint8_t> out;
  const auto* face = font.getEpdFont(style);
  for (const char* at = text; *at; ++at) {
    const EpdGlyph* glyph = face->hasCodepoint(*at) ? face->getGlyph(*at) : nullptr;
    EXPECT_NE(nullptr, glyph) << *at;
    if (!glyph) continue;
    const auto* bytes = reinterpret_cast<const uint8_t*>(glyph);
    out.insert(out.end(), bytes, bytes + sizeof(EpdGlyph));
    const uint8_t* bitmap = font.isOverflowGlyph(glyph) ? font.getOverflowBitmap(glyph) : face->data->bitmap + glyph->dataOffset;
    out.insert(out.end(), bitmap, bitmap + glyph->dataLength);
  }
  return out;
}

size_t bytesOfLoad(SdCardFont& font, const SdCardFont::LoadOptions* options) {
  sdFontTestBytes = 0;
  const bool ok = options ? font.load("fixture", *options) : font.load("fixture");
  EXPECT_TRUE(ok);
  return sdFontTestBytes;
}
}  // namespace

TEST(SdCardFontTest, DefaultLoadKeepsItsContentHash) {
  // Pinned from the code before the light mode: the reader's layout caches are keyed by it.
  makeMultiStyleFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  EXPECT_EQ(0xF5B95A83u, font.contentHash());
  EXPECT_EQ(4, font.styleCount());
  SdCardFont same;
  ASSERT_TRUE(same.load("fixture", SdCardFont::LoadOptions{}));  // the options' default is the full load
  EXPECT_EQ(font.contentHash(), same.contentHash());
}

TEST(SdCardFontTest, LightLoadReadsAFractionOfAFullLoadAndDrawsTheSame) {
  makeMultiStyleFont();
  SdCardFont full;
  const size_t fullBytes = bytesOfLoad(full, nullptr);
  SdCardFont light;
  const SdCardFont::LoadOptions regular{true, 0x01};
  const size_t lightBytes = bytesOfLoad(light, &regular);
  std::printf("multi-style fixture: %zu bytes in the file, full load read %zu, light load read %zu\n",
              sdFontTestFile.size(), fullBytes, lightBytes);
  EXPECT_LT(lightBytes * 4, fullBytes);
  EXPECT_EQ(0u, light.contentHash());
  EXPECT_TRUE(light.hasStyle(0));
  EXPECT_EQ(1, light.styleCount());

  // prewarm(name, regular, with bitmaps, no kern/ligature): what the name renderer asks.
  const char* name = "Hello, world fix x";
  const int missedFull = full.prewarm(name, 0x01, false, false);
  const int missedLight = light.prewarm(name, 0x01, false, false);
  EXPECT_EQ(missedFull, missedLight);
  const EpdFontData* a = full.getEpdFont(0)->data;
  const EpdFontData* b = light.getEpdFont(0)->data;
  EXPECT_EQ(a->advanceY, b->advanceY);
  EXPECT_EQ(a->ascender, b->ascender);
  EXPECT_EQ(a->descender, b->descender);
  EXPECT_EQ(a->is2Bit, b->is2Bit);
  EXPECT_EQ(full.maxInkTop(0), light.maxInkTop(0));
  EXPECT_EQ(prewarmed(full, 0, name), prewarmed(light, 0, name));
  EXPECT_FALSE(prewarmed(light, 0, name).empty());
  for (uint32_t cp : {0x1Fu, 0x20u, 0x17Fu, 0x180u}) EXPECT_EQ(full.getEpdFont(0)->hasCodepoint(cp), light.getEpdFont(0)->hasCodepoint(cp)) << cp;
}

TEST(SdCardFontTest, LightLoadOfAnotherStyleServesThatStyleOnly) {
  makeMultiStyleFont();
  SdCardFont full;
  ASSERT_TRUE(full.load("fixture"));
  SdCardFont light;
  const SdCardFont::LoadOptions bold{true, 0x02};
  ASSERT_TRUE(light.load("fixture", bold));
  EXPECT_TRUE(light.hasStyle(1));
  EXPECT_FALSE(light.hasStyle(0));
  EXPECT_EQ(full.prewarm("Bold x", 0x02, false, false), light.prewarm("Bold x", 0x02, false, false));
  EXPECT_EQ(prewarmed(full, 1, "Bold x"), prewarmed(light, 1, "Bold x"));
  // A style the file has, but not the one asked for, is not set up.
  EXPECT_EQ(nullptr, light.getEpdFont(0));
}

TEST(SdCardFontTest, LightLoadOfStylesTheFileLacksFails) {
  makeMultiStyleFont();
  sdFontTestFile[12] = 1;  // the TOC now lists the regular style alone
  SdCardFont font;
  EXPECT_FALSE(font.load("fixture", SdCardFont::LoadOptions{true, 0x0C}));
  EXPECT_FALSE(font.hasStyle(0));
  EXPECT_TRUE(font.load("fixture", SdCardFont::LoadOptions{true, 0x01}));
}

TEST(SdCardFontTest, AdvancesBuildForAStyleStoredInAnotherSlot) {
  // An italic-only font: style 2 lives in slot 0 of the compact style array.
  makeFont();
  sdFontTestFile[32] = 2;
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_TRUE(font.hasStyle(2));
  ASSERT_FALSE(font.hasStyle(0));
  const auto text = page(FIRST, 100);
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1 << 2));
  for (uint32_t cp = FIRST; cp < FIRST + 100; ++cp) {
    EXPECT_EQ(32 << 4, font.getAdvance(cp, 2));
  }
}

TEST(SdCardFontTest, AFullAdvanceTableSkipsTheScanWithoutAllocating) {
  makeFont(1001);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST, 768).c_str(), 1));
  ASSERT_EQ(32U << 4, font.getAdvance(FIRST + 767, 0));
  const size_t allocationsBefore = nothrowArrayAllocations;
  EXPECT_EQ(0, font.buildAdvanceTable(page(FIRST + 768, 200).c_str(), 1));
  EXPECT_EQ(allocationsBefore, nothrowArrayAllocations);
  EXPECT_EQ(0, font.getAdvance(FIRST + 768, 0));
}

TEST(SdCardFontTest, OnePageWithMoreCodepointsThanTheTableKeepsTheLowestInAnyOrder) {
  constexpr uint32_t CACHE_LIMIT = 768;  // SdCardFont::ADVANCE_CACHE_LIMIT
  makeFont(1001);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  // The top 768 first (ascending, so the largest is not the first held), then
  // the rest descending: each must displace the largest held. Every codepoint
  // appears twice, so repeats must not count twice.
  std::string text;
  for (uint32_t cp = FIRST + 232; cp < FIRST + 1000; ++cp) text += page(cp, 1) + page(cp, 1);
  for (uint32_t cp = FIRST + 231; cp + 1 > FIRST; --cp) text += page(cp, 1) + page(cp, 1);
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  for (uint32_t cp = FIRST; cp < FIRST + 1000; ++cp) {
    ASSERT_EQ(cp < FIRST + CACHE_LIMIT ? 32U << 4 : 0U, font.getAdvance(cp, 0)) << std::hex << cp;
  }
}

TEST(SdCardFontTest, CodepointsTheFontLacksDoNotTakeTableSlotsWithoutAReplacementGlyph) {
  constexpr uint32_t CACHE_LIMIT = 768;  // SdCardFont::ADVANCE_CACHE_LIMIT
  makeFont(1001);
  put32(76, 0xFFFC);  // the last glyph is U+FFFC instead of U+FFFD
  put32(80, 0xFFFC);
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  // 300 codepoints below FIRST that the font lacks, each twice, then a full
  // table's worth it has. Only the latter can be cached, so all should be.
  const std::string lacking = page(0x4E00, 300);
  const std::string text = lacking + lacking + page(FIRST, CACHE_LIMIT);
  EXPECT_EQ(300, font.buildAdvanceTable(text.c_str(), 1));
  for (uint32_t cp = FIRST; cp < FIRST + CACHE_LIMIT; ++cp) {
    ASSERT_EQ(32U << 4, font.getAdvance(cp, 0)) << std::hex << cp;
  }
}

TEST(SdCardFontTest, ShortAdvancePreparationAvoidsLargeScratchAndRepeatedReads) {
  makeFont();
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  const std::string text = page(FIRST, 3);
  failNextArraySize = (768 + 2) * sizeof(uint32_t);
  const int result = font.buildAdvanceTable(text.c_str(), 1);
  failNextArraySize = 0;
  ASSERT_EQ(result, 0);
  for (uint32_t cp = FIRST; cp < FIRST + 3; ++cp) EXPECT_EQ(32 << 4, font.getAdvance(cp, 0));
  sdFontTestReads = 0;
  ASSERT_EQ(font.buildAdvanceTable(text.c_str(), 1), 0);
  EXPECT_EQ(sdFontTestReads, 0u);
}

TEST(SdCardFontTest, SmallCapsPrepareOriginalAndUppercasePackedMetricsWithoutBitmaps) {
  const struct {
    uint32_t first;
    uint32_t last;
    const char* words;
    size_t bytes;
  } cases[] = {
      {0x20, 0x7F, "ab\0z", 5}, {0x0391, 0x03CB, "\xCE\xB1\0\xCE\xB2", 6}, {0x0410, 0x044F, "\xD0\xB0\0\xD1\x8F", 6}};
  for (const auto& fixture : cases) {
    SCOPED_TRACE(fixture.first);
    makeFont(fixture.last - fixture.first + 2);
    put32(64, fixture.first);
    put32(68, fixture.last);
    for (uint32_t cp = fixture.first; cp <= fixture.last; ++cp) {
      put16(64 + 24 + (cp - fixture.first) * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (10 + cp - fixture.first)
                                                                                                  << 4);
    }
    SdCardFont font;
    ASSERT_TRUE(font.load("fixture"));
    const char* segments[] = {fixture.words};
    const size_t lengths[] = {fixture.bytes};
    ASSERT_EQ(font.buildAdvanceTablePacked(segments, lengths, 1, false, false, 1), 0);
    auto* cursor = reinterpret_cast<const uint8_t*>(fixture.words);
    EXPECT_EQ(font.getAdvance(utf8SmallCapsUpper(utf8NextCodepoint(&cursor)), 0), 0);
    ASSERT_EQ(font.buildAdvanceTablePacked(segments, lengths, 1, false, false, 1, nullptr, true), 0);
    cursor = reinterpret_cast<const uint8_t*>(fixture.words);
    const auto* end = cursor + fixture.bytes;
    while (cursor < end) {
      if (*cursor == 0) {
        ++cursor;
        continue;
      }
      const uint32_t cp = utf8NextCodepoint(&cursor);
      EXPECT_EQ(font.getAdvance(cp, 0), (10 + cp - fixture.first) << 4);
      const uint32_t upper = utf8SmallCapsUpper(cp);
      EXPECT_EQ(font.getAdvance(upper, 0), (10 + upper - fixture.first) << 4);
    }
    EXPECT_EQ(font.getStats().bitmapBytes, 0u);
    sdFontTestReads = 0;
    ASSERT_EQ(font.buildAdvanceTablePacked(segments, lengths, 1, false, false, 1, nullptr, true), 0);
    EXPECT_EQ(sdFontTestReads, 0u);
  }
}

TEST(SdCardFontTest, FontsWithTheSameCoverageShareOneIntervalTable) {
  makeFont();
  size_t firstLoad = 0;
  size_t secondLoad = 0;
  auto first = std::make_unique<SdCardFont>();
  {
    const size_t before = nothrowArrayAllocations;
    ASSERT_TRUE(first->load("fixture"));
    firstLoad = nothrowArrayAllocations - before;
  }
  SdCardFont second;
  {
    const size_t before = nothrowArrayAllocations;
    ASSERT_TRUE(second.load("fixture"));
    secondLoad = nothrowArrayAllocations - before;
  }
  EXPECT_EQ(firstLoad - 1, secondLoad);

  // The table outlives the font that loaded it while another font still uses it.
  first.reset();
  EXPECT_TRUE(second.getEpdFont()->hasCodepoint(FIRST));
  EXPECT_TRUE(second.getEpdFont()->hasCodepoint(0xFFFD));
  EXPECT_FALSE(second.getEpdFont()->hasCodepoint(FIRST - 1));
}

TEST(SdCardFontTest, ACorruptGlyphEntryDrawsAsMissing) {
  makeFont();
  // Glyph 3 claims more bytes than its pixels need, as a corrupt .cpfont does.
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  EpdGlyph glyph{};
  std::memcpy(&glyph, sdFontTestFile.data() + GLYPH_OFFSET + 3 * sizeof(glyph), sizeof(glyph));
  glyph.dataLength = 49335;
  std::memcpy(sdFontTestFile.data() + GLYPH_OFFSET + 3 * sizeof(glyph), &glyph, sizeof(glyph));
  SdCardFont font;
  ASSERT_TRUE(font.load("fixture"));
  ASSERT_EQ(0, font.prewarm(page(FIRST, 5).c_str(), 1, false, false, false));
  const auto* data = font.getEpdFont()->data;
  for (uint32_t cp = FIRST; cp < FIRST + 5; ++cp) {
    const EpdGlyph* found = nullptr;
    for (uint32_t i = 0; i < data->intervalCount; ++i) {
      const auto& interval = data->intervals[i];
      if (cp >= interval.first && cp <= interval.last) found = data->glyph + interval.offset + cp - interval.first;
    }
    ASSERT_NE(nullptr, found);
    if (cp == FIRST + 3) {
      EXPECT_EQ(0, found->dataLength);
      EXPECT_EQ(0, found->width);
    } else {
      EXPECT_EQ(BITMAP_BYTES, found->dataLength);
    }
  }
}
