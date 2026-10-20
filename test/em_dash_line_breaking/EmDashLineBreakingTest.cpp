#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Word {
  const char* text;
  bool attachToPrevious = false;
};

struct Options {
  bool hyphenation = false;
  bool focusReading = false;
};

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
};

using Lines = std::vector<std::vector<std::string>>;

// Fixture metrics (stub renderer): every glyph is 8 px wide, combining marks take no advance and a
// space is 4 px.
std::vector<Line> layout(const std::vector<Word>& words, const uint16_t width, const Options options = {}) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, options.hyphenation, options.focusReading, style);
  for (const Word& word : words) text.addWord(word.text, EpdFontFamily::REGULAR, false, word.attachToPrevious);
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
  });
  return lines;
}

Lines wordsOf(const std::vector<Line>& lines) {
  Lines result;
  result.reserve(lines.size());
  for (const auto& line : lines) result.push_back(line.words);
  return result;
}

uint16_t glyphWidth(const char* text) {
  uint16_t width = 0;
  const auto* ptr = reinterpret_cast<const unsigned char*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    if (!utf8IsCombiningMark(cp)) width += 8;
  }
  return width;
}

// Lays out "x <word>" on a line wide enough for the word alone but not for both, so the word can
// only wrap at a token boundary: it is never the first word on the line, which is the one case the
// layout splits a word without hyphenation.
Lines afterLeadingWord(const char* word, const Options options = {}) {
  return wordsOf(layout({{"x"}, {word}}, glyphWidth(word) + 4, options));
}

}  // namespace

TEST(EmDashLineBreaking, BreaksAfterDashBetweenLetters) {
  EXPECT_EQ(afterLeadingWord("foo—bar"), (Lines{{"x", "foo—"}, {"bar"}}));
  EXPECT_EQ(afterLeadingWord("foo-bar"), (Lines{{"x", "foo-"}, {"bar"}}));
}

TEST(EmDashLineBreaking, BreaksAfterDashBetweenVietnameseLetters) {
  EXPECT_EQ(afterLeadingWord("Việt-Nam"), (Lines{{"x", "Việt-"}, {"Nam"}}));
  EXPECT_EQ(afterLeadingWord("nhà—cửa"), (Lines{{"x", "nhà—"}, {"cửa"}}));
}

TEST(EmDashLineBreaking, BreaksAfterDashBetweenDecomposedVietnameseLetters) {
  // NFD input: a + U+0300, u + U+031B + U+0309. The tokens come out composed.
  EXPECT_EQ(afterLeadingWord("nha\xCC\x80—cu\xCC\x9B\xCC\x89"
                             "a"),
            (Lines{{"x", "nhà—"}, {"cửa"}}));
}

TEST(EmDashLineBreaking, LetterWithUncomposableMarkCountsAsLetter) {
  // q + U+0303 has no precomposed form, so the mark stays between the letter and the dash.
  EXPECT_EQ(afterLeadingWord("q\xCC\x83—bar"), (Lines{{"x", "q\xCC\x83—"}, {"bar"}}));
}

TEST(EmDashLineBreaking, UnbrokenDashJoinKeepsNoGap) {
  const auto lines = layout({{"foo—bar"}}, 80);
  ASSERT_EQ(wordsOf(lines), (Lines{{"foo—", "bar"}}));
  // Justification must not stretch the join: "bar" sits right after "foo—".
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 32}));
}

TEST(EmDashLineBreaking, DashRunStaysOneUnit) {
  EXPECT_EQ(afterLeadingWord("a--b"), (Lines{{"x", "a--"}, {"b"}}));
  EXPECT_EQ(afterLeadingWord("foo——bar"), (Lines{{"x", "foo——"}, {"bar"}}));
}

TEST(EmDashLineBreaking, EveryDashBetweenLettersCanBreak) {
  EXPECT_EQ(afterLeadingWord("foo—bar—baz"), (Lines{{"x", "foo—", "bar—"}, {"baz"}}));
}

TEST(EmDashLineBreaking, HyphenationOnBreaksAtDashWithoutInsertedHyphen) {
  EXPECT_EQ(afterLeadingWord("foo—bar", {.hyphenation = true}), (Lines{{"x", "foo—"}, {"bar"}}));
}

TEST(EmDashLineBreaking, DashNotBetweenLettersStaysAttached) {
  for (const char* word : {"10-20", "-foo", "foo-", "—foo", "foo—.", "(—)", "foo-10", "10-foo", "foo—(bar)"}) {
    EXPECT_EQ(afterLeadingWord(word), (Lines{{"x"}, {word}})) << word;
  }
}

TEST(EmDashLineBreaking, DashBetweenLettersAcrossInlineElementsBreaks) {
  // "foo<i>—bar</i>" and "foo—<i>bar</i>" reach ParsedText as two glued words.
  EXPECT_EQ(wordsOf(layout({{"x"}, {"foo"}, {"—bar", true}}, 60)), (Lines{{"x", "foo", "—"}, {"bar"}}));
  EXPECT_EQ(wordsOf(layout({{"x"}, {"foo—"}, {"bar", true}}, 60)), (Lines{{"x", "foo—"}, {"bar"}}));
  EXPECT_EQ(wordsOf(layout({{"x"}, {"10"}, {"-foo", true}}, 52)), (Lines{{"x"}, {"10", "-foo"}}));
  EXPECT_EQ(wordsOf(layout({{"x"}, {"foo-"}, {"10", true}}, 52)), (Lines{{"x"}, {"foo-", "10"}}));
}

TEST(EmDashLineBreaking, FocusReadingFollowsTheSameRule) {
  // Focus Reading splits words into letter and punctuation tokens of its own.
  EXPECT_EQ(afterLeadingWord("foo—bar", {.focusReading = true}), (Lines{{"x", "foo", "—"}, {"bar"}}));
  EXPECT_EQ(afterLeadingWord("-foo", {.focusReading = true}), (Lines{{"x"}, {"-", "foo"}}));
}
