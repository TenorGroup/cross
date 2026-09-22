// Reopening a saved quote on its page for "Reselect on the book page": the selector starts
// with the old range marked, so a single Confirm saves the quote unchanged. When the words
// are not wholly on the page the selector starts a fresh selection instead.
#include <gtest/gtest.h>

#include <vector>

#include "activities/reader/QuoteReselect.h"

namespace {

constexpr int SPINE = 3;

// Five words from offset 1000, one separator codepoint between them, laid out the way
// pageWords() lays them out: "Con" 1000, "mot" 1004, "trang" 1008, "thu" 1014, "hai" 1018.
std::vector<quotes::PageWord> pageOfFive() {
  const uint16_t lengths[] = {3, 3, 5, 3, 3};
  std::vector<quotes::PageWord> words;
  uint32_t offset = 1000;
  for (const uint16_t length : lengths) {
    quotes::PageWord word;
    word.text = "w";
    word.offset = offset;
    word.codepoints = length;
    words.push_back(word);
    offset += length + 1u;
  }
  return words;
}

QuoteRecord anchored(const uint32_t start, const uint32_t end, const int spine = SPINE) {
  QuoteRecord quote;
  quote.spine = spine;
  quote.hasAnchor = true;
  quote.anchorStart = start;
  quote.anchorEnd = end;
  return quote;
}

TEST(QuoteReselect, RangeSavedOnThisPageComesBackAsTheSameWords) {
  const auto words = pageOfFive();
  QuoteRecord quote;
  quote.spine = SPINE;
  quotes::setAnchor(quote, words, 1, 3);
  size_t first = 99, last = 99;
  ASSERT_TRUE(quotes::reselectRange(quote, SPINE, words, first, last));
  EXPECT_EQ(first, 1u);
  EXPECT_EQ(last, 3u);
}

TEST(QuoteReselect, OneWordQuoteComesBackAsThatWord) {
  const auto words = pageOfFive();
  size_t first = 99, last = 99;
  ASSERT_TRUE(quotes::reselectRange(anchored(1008, 1013), SPINE, words, first, last));
  EXPECT_EQ(first, 2u);
  EXPECT_EQ(last, 2u);
}

TEST(QuoteReselect, WholePageQuoteComesBackAsEveryWord) {
  const auto words = pageOfFive();
  size_t first = 99, last = 99;
  ASSERT_TRUE(quotes::reselectRange(anchored(1000, 1021), SPINE, words, first, last));
  EXPECT_EQ(first, 0u);
  EXPECT_EQ(last, 4u);
}

TEST(QuoteReselect, QuoteWithoutAnchorIsMissing) {
  const auto words = pageOfFive();
  QuoteRecord quote = anchored(1004, 1013);
  quote.hasAnchor = false;
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::reselectRange(quote, SPINE, words, first, last));
}

TEST(QuoteReselect, QuoteOfAnotherSpineItemIsMissing) {
  const auto words = pageOfFive();
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::reselectRange(anchored(1004, 1013, SPINE + 1), SPINE, words, first, last));
}

TEST(QuoteReselect, QuoteElsewhereInTheSpineItemIsMissing) {
  const auto words = pageOfFive();
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::reselectRange(anchored(200, 260), SPINE, words, first, last));
  EXPECT_FALSE(quotes::reselectRange(anchored(1500, 1560), SPINE, words, first, last));
}

// After the text was laid out again (another font size) a quote can straddle the page edge.
// Marking only the part on this page would save a shorter quote on a single Confirm, so the
// selector starts fresh instead.
TEST(QuoteReselect, QuoteStartingOnThePreviousPageIsMissing) {
  const auto words = pageOfFive();
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::reselectRange(anchored(990, 1007), SPINE, words, first, last));
}

TEST(QuoteReselect, QuoteRunningOntoTheNextPageIsMissing) {
  const auto words = pageOfFive();
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::reselectRange(anchored(1014, 1030), SPINE, words, first, last));
}

TEST(QuoteReselect, EmptyRangeAndEmptyPageAreMissing) {
  const auto words = pageOfFive();
  size_t first = 0, last = 0;
  EXPECT_FALSE(quotes::reselectRange(anchored(1008, 1008), SPINE, words, first, last));
  EXPECT_FALSE(quotes::reselectRange(anchored(1008, 1013), SPINE, {}, first, last));
}

}  // namespace
