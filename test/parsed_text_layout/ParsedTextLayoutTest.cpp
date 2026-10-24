// What the merged layout owes the reader of v1.0.76: the word gap at every spacing step, the large
// initial as a property of its line (not a bit of the word's style), and one answer to "which drop
// cap path runs".
#include <Epub/ParsedText.h>
#include <Epub/ReaderSpacing.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <HalStorage.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Placed {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
  std::vector<EpdFontFamily::Style> styles;
  std::vector<bool> discretionaryHyphen;
  uint16_t dropCapHeight = 0;
};

struct Layout {
  int spaceAdvance = 4;
  uint8_t wordSpacingPercent = 100;
  int8_t characterSpacing = 0;
  bool hyphenation = false;
  uint16_t dropCap = 0;
  uint16_t width = 400;
  CssTextAlign alignment = CssTextAlign::Left;
};

std::vector<Placed> run(const std::vector<std::pair<const char*, EpdFontFamily::Style>>& words, const Layout& in) {
  GfxRenderer renderer;
  renderer.spaceAdvance = in.spaceAdvance;
  BlockStyle style;
  style.alignment = in.alignment;
  style.textIndentDefined = true;
  ParsedText text(in.hyphenation, false, style, 0);
  if (in.dropCap) text.enableDropCap(in.dropCap);
  for (const auto& [word, wordStyle] : words) EXPECT_TRUE(text.addWord(word, wordStyle));
  std::vector<Placed> lines;
  EXPECT_TRUE(text.layoutAndExtractLines(
      renderer, 0, in.width,
      [&](std::unique_ptr<TextBlock> block, auto) {
        auto& line = lines.emplace_back();
        line.dropCapHeight = block->getDropCapHeight();
        for (uint16_t i = 0; i < block->wordCount(); ++i) {
          line.words.emplace_back(block->wordText(i));
          line.xpos.push_back(block->wordXpos(i));
          line.styles.push_back(block->wordStyle(i));
          line.discretionaryHyphen.push_back(block->wordHasDiscretionaryHyphen(i));
        }
      },
      true, in.characterSpacing, in.wordSpacingPercent));
  return lines;
}

constexpr auto R = EpdFontFamily::REGULAR;

}  // namespace

// The reader's word spacing steps (50, 75, 100, 150, 200 percent) are exact pixels: the space
// advance times the step, cut at the integer. Rounding would put 7 px at 50 percent on 4, not 3.
TEST(ParsedTextLayout, WordGapIsTheSpaceAdvanceTimesTheStepCutToPixels) {
  const struct {
    uint8_t percent;
    int space;
    int gap;
  } cases[] = {{50, 7, 3}, {75, 7, 5}, {100, 7, 7}, {150, 7, 10}, {200, 7, 14}, {50, 9, 4}, {75, 9, 6}, {150, 9, 13}};
  for (const auto& c : cases) {
    Layout in;
    in.spaceAdvance = c.space;
    in.wordSpacingPercent = c.percent;
    const auto lines = run({{"ab", R}, {"cd", R}}, in);
    ASSERT_EQ(lines.size(), 1u) << c.percent;
    ASSERT_EQ(lines[0].xpos.size(), 2u);
    EXPECT_EQ(lines[0].xpos[1], 16 + c.gap) << "step " << int(c.percent) << " space " << c.space;
  }
}

// Letter spacing spaces every glyph advance, the space's neighbours too: a word is its glyphs plus
// the spacing between them, and the gap between two words is the space plus the spacing twice.
TEST(ParsedTextLayout, LetterSpacingSpacesBothSidesOfAWordGap) {
  for (const int8_t spacing : {-2, -1, 0, 1, 2}) {
    Layout in;
    in.characterSpacing = spacing;
    const auto lines = run({{"ab", R}, {"cd", R}}, in);
    ASSERT_EQ(lines.size(), 1u) << int(spacing);
    // "ab" is 8 + spacing + 8; the gap is the space (4) and the spacing on each side of it.
    EXPECT_EQ(lines[0].xpos[1], 16 + spacing + 4 + 2 * spacing) << int(spacing);
  }
}

// The initial belongs to the line: the block of its line carries the height, word 0 keeps its own
// style bits, and the lines under it do not carry the height.
TEST(ParsedTextLayout, DropCapIsMarkedOnTheBlockNotOnTheWordStyle) {
  Layout in;
  in.dropCap = 32;
  in.width = 200;
  const auto lines = run({{"Once", R}, {"upon", R}, {"a", R}, {"time", R}, {"there", R}, {"was", R}, {"a", R},
                          {"king", R}, {"who", R}, {"lived", R}, {"alone", R}},
                         in);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines[0].dropCapHeight, 32);
  EXPECT_EQ(lines[0].styles[0], R);
  for (size_t i = 1; i < lines.size(); ++i) EXPECT_EQ(lines[i].dropCapHeight, 0) << i;
}

// Bit 128 of the style is small caps; the initial of a small-caps paragraph keeps it.
TEST(ParsedTextLayout, SmallCapsInitialKeepsItsBitAndTheDropCap) {
  Layout in;
  in.dropCap = 32;
  in.width = 200;
  const auto lines = run({{"Once", EpdFontFamily::SMALL_CAPS}, {"upon", R}, {"a", R}}, in);
  ASSERT_GE(lines.size(), 1u);
  EXPECT_EQ(lines[0].dropCapHeight, 32);
  EXPECT_TRUE(lines[0].styles[0] & EpdFontFamily::SMALL_CAPS);
}

// A first word that carries soft hyphens takes the initial and splits at one of them without the
// discretionary-hyphen flag ever landing on the initial's own word.
TEST(ParsedTextLayout, InitialWordWithSoftHyphensKeepsItsFlagsApart) {
  Layout in;
  in.dropCap = 32;
  in.width = 100;
  in.hyphenation = true;
  const auto lines = run({{"Re\xC2\xAD" "spect", R}, {"for", R}, {"all", R}}, in);
  ASSERT_GE(lines.size(), 1u);
  EXPECT_EQ(lines[0].dropCapHeight, 32);
  EXPECT_FALSE(lines[0].discretionaryHyphen[0]);
  EXPECT_EQ(lines[0].styles[0], R);
}

// Off is no drop cap at all; Default and Large draw the reader's own initial for the chapter's first
// paragraph; nothing the stylesheet asks for (::first-letter, a floated or oversized first span)
// ever does.
TEST(ParsedTextLayout, OneDecisionForTheDropCapPath) {
  using Source = ParsedText::DropCapSource;
  constexpr int line = 40;
  for (const Source source : {Source::ChapterInitial, Source::CssFirstLetter, Source::CssSpan}) {
    EXPECT_EQ(ParsedText::dropCapHeightFor(source, readerSpacing::DROP_CAP_OFF, line), 0);
  }
  EXPECT_EQ(ParsedText::dropCapHeightFor(Source::ChapterInitial, readerSpacing::DROP_CAP_DEFAULT, line),
            readerSpacing::dropCapHeight(readerSpacing::DROP_CAP_DEFAULT, line));
  EXPECT_EQ(ParsedText::dropCapHeightFor(Source::ChapterInitial, readerSpacing::DROP_CAP_LARGE, line),
            readerSpacing::dropCapHeight(readerSpacing::DROP_CAP_LARGE, line));
  EXPECT_GT(ParsedText::dropCapHeightFor(Source::ChapterInitial, readerSpacing::DROP_CAP_LARGE, line),
            ParsedText::dropCapHeightFor(Source::ChapterInitial, readerSpacing::DROP_CAP_DEFAULT, line));
  for (const Source source : {Source::CssFirstLetter, Source::CssSpan}) {
    for (const uint8_t mode : {readerSpacing::DROP_CAP_DEFAULT, readerSpacing::DROP_CAP_LARGE}) {
      EXPECT_EQ(ParsedText::dropCapHeightFor(source, mode, line), 0);
    }
  }
}

// Vietnamese small caps: the letters of U+1EA0..U+1EF9 go to their capitals (a with dot below,
// e with tilde, the horned o and u).
TEST(ParsedTextLayout, VietnameseSmallCapsMapToTheirCapitals) {
  EXPECT_EQ(utf8SmallCapsUpper(0x1EA1), 0x1EA0u);  // a with dot below
  EXPECT_EQ(utf8SmallCapsUpper(0x1EBD), 0x1EBCu);  // e with tilde
  EXPECT_EQ(utf8SmallCapsUpper(0x1EF9), 0x1EF8u);  // y with tilde
  EXPECT_EQ(utf8SmallCapsUpper(0x1EA0), 0x1EA0u);  // already a capital
  EXPECT_EQ(utf8SmallCapsUpper(0x01A1), 0x01A0u);  // o with horn
  EXPECT_EQ(utf8SmallCapsUpper(0x01B0), 0x01AFu);  // u with horn
  EXPECT_EQ(utf8SmallCapsUpper(0x1EFA), 0x1EFAu);  // past the block
}

namespace {
// Lays one paragraph out and returns its first block after a round trip through the page file.
std::unique_ptr<TextBlock> roundTrip(const std::string& name, const bool dropCap, const bool ruby, const int8_t spacing) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  style.fontId = 0;
  ParsedText text(false, false, style, 0);
  if (dropCap) text.enableDropCap(32);
  EXPECT_TRUE(text.addWord("Once", R));
  EXPECT_TRUE(text.addWord("more", R));
  if (ruby) text.setRubyForWordAt(1, "ru");
  std::unique_ptr<TextBlock> kept;
  EXPECT_TRUE(text.layoutAndExtractLines(
      renderer, 0, 400, [&](std::unique_ptr<TextBlock> block, auto) { if (!kept) kept = std::move(block); }, true, spacing,
      100));
  EXPECT_TRUE(kept && kept->valid());
  const std::string path = (std::filesystem::temp_directory_path() / ("parsed-text-layout-" + name)).string();
  HalFile out;
  EXPECT_TRUE(Storage.openFileForWrite("TST", path, out));
  EXPECT_TRUE(kept->serialize(out));
  out.close();
  HalFile in;
  EXPECT_TRUE(Storage.openFileForRead("TST", path, in));
  auto back = TextBlock::deserialize(in);
  in.close();
  Storage.remove(path);
  return back;
}
}  // namespace

// The page file keeps what the line needs to be drawn again: the height of its initial, the spacing,
// the ruby of the word it belongs to.
TEST(ParsedTextLayout, TheBlockSurvivesTheTripThroughThePageFile) {
  const auto back = roundTrip("drop", true, false, 2);
  ASSERT_TRUE(back);
  EXPECT_EQ(back->getDropCapHeight(), 32);
  EXPECT_EQ(back->getBlockStyle().characterSpacing, 2);
  EXPECT_STREQ(back->wordText(0), "Once");
  EXPECT_FALSE(back->hasRuby());

  const auto withRuby = roundTrip("ruby", false, true, -1);
  ASSERT_TRUE(withRuby);
  EXPECT_EQ(withRuby->getDropCapHeight(), 0);
  EXPECT_EQ(withRuby->getBlockStyle().characterSpacing, -1);
  ASSERT_TRUE(withRuby->hasRuby());
  EXPECT_EQ(withRuby->getRubyText(1), "ru");
  EXPECT_TRUE(withRuby->getRubyText(0).empty());
}
