// Placing the words of a rendered page in the book's visible text, and finding which
// of them a saved quote covers. The offsets a quote stores when it is saved and the
// offsets computed when a page is drawn come from this one walk, so the two agree.
#include <QuoteHighlight.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int MARGIN_LEFT = 10;
constexpr int MARGIN_TOP = 40;
constexpr int ASCENDER = 12;
constexpr uint32_t PAGE_START = 1000;

std::unique_ptr<PageLine> line(const std::vector<std::string>& words, const std::vector<int16_t>& xpos,
                               const int16_t x, const int16_t y, std::vector<std::string> ruby = {}) {
  std::vector<EpdFontFamily::Style> styles(words.size(), EpdFontFamily::REGULAR);
  auto block = std::make_unique<TextBlock>(words, xpos, styles, std::vector<uint8_t>{}, std::vector<uint16_t>{},
                                           BlockStyle(), std::move(ruby));
  return std::make_unique<PageLine>(std::move(block), x, y);
}

// "mot" and "thu" carry Vietnamese marks, so a byte count and a codepoint count differ:
// the offsets must follow codepoints, the unit the reader already stores progress in.
Page samplePage() {
  Page page;
  page.visibleTextOffset = PAGE_START;
  page.elements.push_back(line({"Con", "m\xe1\xbb\x99t", "trang"}, {0, 40, 80}, 5, 100));
  page.elements.push_back(line({"th\xe1\xbb\xa9", "hai"}, {0, 40}, 5, 130));
  return page;
}

std::vector<quotes::PageWord> sampleWords(const Page& page) {
  std::vector<quotes::PageWord> words;
  quotes::pageWords(page, MARGIN_LEFT, MARGIN_TOP, ASCENDER, words);
  return words;
}

TEST(QuoteHighlight, PageWordsCountCodepointsAndOneSeparator) {
  const Page page = samplePage();
  std::vector<quotes::PageWord> words;
  const uint32_t end = quotes::pageWords(page, MARGIN_LEFT, MARGIN_TOP, ASCENDER, words);

  ASSERT_EQ(words.size(), 5u);
  EXPECT_STREQ(words[0].text, "Con");
  EXPECT_EQ(words[0].offset, 1000u);
  EXPECT_EQ(words[0].codepoints, 3u);
  EXPECT_EQ(words[1].offset, 1004u);
  EXPECT_EQ(words[1].codepoints, 3u);
  EXPECT_EQ(words[2].offset, 1008u);
  EXPECT_EQ(words[2].codepoints, 5u);
  EXPECT_EQ(words[3].offset, 1014u);
  EXPECT_EQ(words[3].codepoints, 3u);
  EXPECT_EQ(words[4].offset, 1018u);
  EXPECT_EQ(words[4].codepoints, 3u);
  EXPECT_EQ(end, 1021u);
}

// Same box the quote selector draws: line position plus the page margins.
TEST(QuoteHighlight, PageWordsPlaceBoxesLikeTheSelector) {
  const std::vector<quotes::PageWord> words = sampleWords(samplePage());
  ASSERT_EQ(words.size(), 5u);
  EXPECT_EQ(words[0].x, 5 + 0 + MARGIN_LEFT);
  EXPECT_EQ(words[0].y, 100 + MARGIN_TOP);
  EXPECT_EQ(words[2].x, 5 + 80 + MARGIN_LEFT);
  EXPECT_EQ(words[4].x, 5 + 40 + MARGIN_LEFT);
  EXPECT_EQ(words[4].y, 130 + MARGIN_TOP);
}

TEST(QuoteHighlight, RubyLineShiftsTheBoxDownLikeTheSelector) {
  Page page;
  page.visibleTextOffset = PAGE_START;
  page.elements.push_back(line({"kanji", "word"}, {0, 40}, 5, 100, {"furi", ""}));
  const std::vector<quotes::PageWord> words = sampleWords(page);
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].y, 100 + MARGIN_TOP + ASCENDER / 2);
}

// The selector accepts every non-empty word when it is picking a quote, so an empty one
// must not consume an offset either.
TEST(QuoteHighlight, EmptyWordsAreSkipped) {
  Page page;
  page.visibleTextOffset = PAGE_START;
  page.elements.push_back(line({"one", "", "two"}, {0, 30, 40}, 0, 0));
  const std::vector<quotes::PageWord> words = sampleWords(page);
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].offset, 1000u);
  EXPECT_EQ(words[1].offset, 1004u);
}

TEST(QuoteHighlight, AnchorRoundTripsToTheSameWords) {
  const Page page = samplePage();
  const std::vector<quotes::PageWord> words = sampleWords(page);
  QuoteRecord quote;
  quote.spine = 3;
  quotes::setAnchor(quote, words, 1, 2);
  EXPECT_TRUE(quote.hasAnchor);
  EXPECT_EQ(quote.anchorStart, 1004u);
  EXPECT_EQ(quote.anchorEnd, 1013u);

  const QuoteAnchor anchor{quote.spine, quote.anchorStart, quote.anchorEnd};
  size_t first = 0, last = 0;
  ASSERT_TRUE(quotes::coveredWords(anchor, 3, words, first, last));
  EXPECT_EQ(first, 1u);
  EXPECT_EQ(last, 2u);
}

TEST(QuoteHighlight, AnchorFromAnotherSpineIsNotDrawn) {
  const std::vector<quotes::PageWord> words = sampleWords(samplePage());
  const QuoteAnchor anchor{4, 1004, 1013};
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::coveredWords(anchor, 3, words, first, last));
}

TEST(QuoteHighlight, AnchorOutsideThePageIsNotDrawn) {
  const std::vector<quotes::PageWord> words = sampleWords(samplePage());
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::coveredWords(QuoteAnchor{3, 900, 1000}, 3, words, first, last));
  EXPECT_FALSE(quotes::coveredWords(QuoteAnchor{3, 1021, 1040}, 3, words, first, last));
}

// A quote that started on the page before this one still highlights the part that
// landed here.
TEST(QuoteHighlight, AnchorCrossingThePageStartHighlightsThePartOnThisPage) {
  const std::vector<quotes::PageWord> words = sampleWords(samplePage());
  size_t first = 0, last = 0;
  ASSERT_TRUE(quotes::coveredWords(QuoteAnchor{3, 980, 1006}, 3, words, first, last));
  EXPECT_EQ(first, 0u);
  EXPECT_EQ(last, 1u);
}

TEST(QuoteHighlight, AnchorCrossingThePageEndHighlightsThePartOnThisPage) {
  const std::vector<quotes::PageWord> words = sampleWords(samplePage());
  size_t first = 0, last = 0;
  ASSERT_TRUE(quotes::coveredWords(QuoteAnchor{3, 1015, 1200}, 3, words, first, last));
  EXPECT_EQ(first, 3u);
  EXPECT_EQ(last, 4u);
}

TEST(QuoteHighlight, EmptyPageDrawsNothing) {
  const Page page;
  std::vector<quotes::PageWord> words;
  EXPECT_EQ(quotes::pageWords(page, MARGIN_LEFT, MARGIN_TOP, ASCENDER, words), 0u);
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::coveredWords(QuoteAnchor{0, 0, 10}, 0, words, first, last));
}

// The reader asks this before walking a page, so a chapter with no quotes behind or on
// it costs nothing per page turn.
TEST(QuoteHighlight, AnyAnchorAtOrAfterFiltersSpineAndPosition) {
  const std::vector<QuoteAnchor> anchors{{3, 1004, 1013}, {5, 20, 40}};
  EXPECT_TRUE(quotes::anyAnchorAtOrAfter(anchors, 3, 1000));
  EXPECT_TRUE(quotes::anyAnchorAtOrAfter(anchors, 3, 1012));
  EXPECT_FALSE(quotes::anyAnchorAtOrAfter(anchors, 3, 1013));
  EXPECT_FALSE(quotes::anyAnchorAtOrAfter(anchors, 4, 0));
  EXPECT_FALSE(quotes::anyAnchorAtOrAfter({}, 3, 0));
}

// The band merge below is what makes a highlight read as one continuous mark: the
// selector used to fill one box per word, which left the spaces between the words white
// and turned a sentence into a row of separate blocks.
constexpr int LINE_HEIGHT = 20;

std::vector<quotes::WordBox> boxes(const std::vector<std::array<int16_t, 3>>& raw) {
  std::vector<quotes::WordBox> out;
  for (const auto& item : raw) out.push_back(quotes::WordBox{item[0], item[1], item[2]});
  return out;
}

TEST(QuoteHighlightBands, WordsOnOneLineBecomeOneBandOverTheSpacesBetweenThem) {
  std::vector<quotes::HighlightBand> bands;
  quotes::highlightBands(boxes({{10, 100, 30}, {50, 100, 20}, {80, 100, 25}}), LINE_HEIGHT, bands);
  ASSERT_EQ(bands.size(), 1u);
  EXPECT_EQ(bands[0].x, 9);
  EXPECT_EQ(bands[0].y, 99);
  EXPECT_EQ(bands[0].width, 97);  // 10 to 105, plus one pixel of padding each side
  EXPECT_EQ(bands[0].height, LINE_HEIGHT + 2);
}

TEST(QuoteHighlightBands, EachLineOfTheSelectionGetsItsOwnBand) {
  std::vector<quotes::HighlightBand> bands;
  quotes::highlightBands(boxes({{10, 100, 30}, {50, 100, 20}, {12, 130, 40}}), LINE_HEIGHT, bands);
  ASSERT_EQ(bands.size(), 2u);
  EXPECT_EQ(bands[0].x, 9);
  EXPECT_EQ(bands[0].y, 99);
  EXPECT_EQ(bands[0].width, 62);
  EXPECT_EQ(bands[1].x, 11);
  EXPECT_EQ(bands[1].y, 129);
  EXPECT_EQ(bands[1].width, 42);
}

// A single word must still land on the box the selector drew before this change, so a
// one-word quote looks exactly as it did.
TEST(QuoteHighlightBands, OneWordKeepsTheBoxItAlreadyHad) {
  std::vector<quotes::HighlightBand> bands;
  quotes::highlightBands(boxes({{40, 60, 18}}), LINE_HEIGHT, bands);
  ASSERT_EQ(bands.size(), 1u);
  EXPECT_EQ(bands[0].x, 39);
  EXPECT_EQ(bands[0].y, 59);
  EXPECT_EQ(bands[0].width, 20);
  EXPECT_EQ(bands[0].height, LINE_HEIGHT + 2);
}

// A word pushed to the left of the one before it on the SAME baseline (bidi runs) must
// not shrink the band: the band spans the whole line, whichever word sits furthest out.
TEST(QuoteHighlightBands, BandSpansTheWholeLineWhateverOrderTheWordsCome) {
  std::vector<quotes::HighlightBand> bands;
  quotes::highlightBands(boxes({{80, 100, 25}, {10, 100, 30}}), LINE_HEIGHT, bands);
  ASSERT_EQ(bands.size(), 1u);
  EXPECT_EQ(bands[0].x, 9);
  EXPECT_EQ(bands[0].width, 97);
}

TEST(QuoteHighlightBands, NoSelectionDrawsNothingAndClearsTheCallersBuffer) {
  std::vector<quotes::HighlightBand> bands{{1, 2, 3, 4}};
  quotes::highlightBands({}, LINE_HEIGHT, bands);
  EXPECT_TRUE(bands.empty());
}

// Trimming a saved quote must keep its highlight on exactly the words that are left. The
// page below mixes plain words, precomposed Vietnamese ("một", "thứ") and decomposed
// Vietnamese ("tiếng" and "Việt" written as base letters plus combining marks), so a
// trim that counted bytes, or composed the marks before counting, would drift.
Page trimPage() {
  Page page;
  page.visibleTextOffset = PAGE_START;
  page.elements.push_back(line({"Con", "m\xe1\xbb\x99t", "ti" "e\xcc\x82\xcc\x81" "ng", "trang"}, {0, 40, 80, 130}, 5,
                               100));
  page.elements.push_back(line({"th\xe1\xbb\xa9", "Vie\xcc\xa3\xcc\x82t", "hai", "ba."}, {0, 40, 90, 130}, 5, 130));
  return page;
}

// The saved text, built the way the selector builds it: the chosen words joined by one space.
std::string joined(const std::vector<quotes::PageWord>& words, size_t first, size_t last) {
  std::string text;
  for (size_t i = first; i <= last; i++) {
    if (i > first) text += ' ';
    text += words[i].text;
  }
  return text;
}

TEST(QuoteTrim, WordCountFollowsTheSelectorsSpaces) {
  EXPECT_EQ(quotes::wordCount(""), 0u);
  EXPECT_EQ(quotes::wordCount("one"), 1u);
  EXPECT_EQ(quotes::wordCount("m\xe1\xbb\x99t hai ba"), 3u);
}

TEST(QuoteTrim, TrimmedAnchorCoversExactlyTheWordsLeft) {
  const Page page = trimPage();
  const std::vector<quotes::PageWord> words = sampleWords(page);
  ASSERT_EQ(words.size(), 8u);
  // "tiếng" decomposed is t, i, e, two marks, n, g: seven codepoints for five letters.
  ASSERT_EQ(words[2].codepoints, 7u);
  size_t checked = 0;
  for (size_t a = 0; a < words.size(); a++) {
    for (size_t b = a; b < words.size(); b++) {
      const size_t count = b - a + 1;
      for (size_t k = 0; k < count; k++) {
        for (size_t j = 0; k + j < count; j++) {
          QuoteRecord quote;
          quote.spine = 3;
          quote.text = joined(words, a, b);
          quotes::setAnchor(quote, words, a, b);
          ASSERT_EQ(quotes::wordCount(quote.text), count);
          ASSERT_TRUE(quotes::trimWords(quote, k, j)) << a << " " << b << " " << k << " " << j;
          EXPECT_EQ(quote.text, joined(words, a + k, b - j));
          size_t first = 0, last = 0;
          ASSERT_TRUE(quotes::coveredWords(QuoteAnchor{3, quote.anchorStart, quote.anchorEnd}, 3, words, first, last));
          EXPECT_EQ(first, a + k) << a << " " << b << " " << k << " " << j;
          EXPECT_EQ(last, b - j) << a << " " << b << " " << k << " " << j;
          // And the anchor is the very one the selector would stamp on the shorter choice.
          QuoteRecord direct;
          quotes::setAnchor(direct, words, a + k, b - j);
          EXPECT_EQ(quote.anchorStart, direct.anchorStart);
          EXPECT_EQ(quote.anchorEnd, direct.anchorEnd);
          checked++;
        }
      }
    }
  }
  EXPECT_GT(checked, 100u);
}

TEST(QuoteTrim, TrimmingEveryWordIsRefusedAndLeavesTheQuoteAlone) {
  const std::vector<quotes::PageWord> words = sampleWords(trimPage());
  QuoteRecord quote;
  quote.spine = 3;
  quote.text = joined(words, 1, 3);
  quotes::setAnchor(quote, words, 1, 3);
  const QuoteRecord before = quote;
  EXPECT_FALSE(quotes::trimWords(quote, 2, 1));
  EXPECT_FALSE(quotes::trimWords(quote, 3, 0));
  EXPECT_FALSE(quotes::trimWords(quote, 0, 5));
  EXPECT_FALSE(quotes::trimWords(quote, static_cast<size_t>(-1), 2));
  EXPECT_EQ(quote.text, before.text);
  EXPECT_EQ(quote.anchorStart, before.anchorStart);
  EXPECT_EQ(quote.anchorEnd, before.anchorEnd);
}

// A quote kept before anchors existed has only its text to trim.
TEST(QuoteTrim, UnanchoredQuoteTrimsTextOnly) {
  QuoteRecord quote;
  quote.text = "one two three";
  EXPECT_TRUE(quotes::trimWords(quote, 1, 1));
  EXPECT_EQ(quote.text, "two");
  EXPECT_FALSE(quote.hasAnchor);
  EXPECT_EQ(quote.anchorStart, 0u);
  EXPECT_EQ(quote.anchorEnd, 0u);
}

}  // namespace
