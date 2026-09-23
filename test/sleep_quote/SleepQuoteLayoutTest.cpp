#include <gtest/gtest.h>

#include <set>
#include <string>

#include "activities/boot_sleep/SleepQuoteLayout.h"

namespace {

// One column per codepoint, ten pixels each: enough to check where a line is cut without a
// font.
int columns(const std::string& text) {
  int count = 0;
  for (const unsigned char c : text) count += (c & 0xC0) != 0x80;
  return count * 10;
}

}  // namespace

TEST(SleepQuotePick, EmptyStoreHasNothingToShow) {
  EXPECT_EQ(sleepquote::pickIndex(0, sleepquote::NO_INDEX, 7), sleepquote::NO_INDEX);
}

TEST(SleepQuotePick, SingleQuoteIsShownAgain) {
  for (uint32_t draw = 0; draw < 5; draw++) EXPECT_EQ(sleepquote::pickIndex(1, 0, draw), 0u);
}

TEST(SleepQuotePick, NeverRepeatsTheLastQuoteAndReachesEveryOther) {
  std::set<size_t> seen;
  for (uint32_t draw = 0; draw < 64; draw++) {
    const size_t index = sleepquote::pickIndex(4, 2, draw);
    ASSERT_LT(index, 4u);
    EXPECT_NE(index, 2u) << "draw " << draw;
    seen.insert(index);
  }
  EXPECT_EQ(seen, (std::set<size_t>{0, 1, 3}));
}

TEST(SleepQuotePick, LastQuoteGoneFromTheStoreLeavesAllCandidates) {
  std::set<size_t> seen;
  for (uint32_t draw = 0; draw < 64; draw++) seen.insert(sleepquote::pickIndex(4, sleepquote::NO_INDEX, draw));
  EXPECT_EQ(seen, (std::set<size_t>{0, 1, 2, 3}));
  // An index past the end (the store shrank) is the same as no last quote.
  seen.clear();
  for (uint32_t draw = 0; draw < 64; draw++) seen.insert(sleepquote::pickIndex(4, 9, draw));
  EXPECT_EQ(seen, (std::set<size_t>{0, 1, 2, 3}));
}

TEST(SleepQuoteFit, BodyLineLimitLeavesTheCoverRowFree) {
  // 596 row top, 30 px gap, 138 body top: 428 px of body.
  EXPECT_EQ(sleepquote::bodyLineLimit(50), 8);
  EXPECT_EQ(sleepquote::bodyLineLimit(44), 9);
  EXPECT_EQ(sleepquote::bodyLineLimit(39), 10);
  EXPECT_EQ(sleepquote::bodyLineLimit(0), 0);
}

TEST(SleepQuoteFit, ShortQuoteKeepsTheLargestSizeAndWrapsOnce) {
  const int heights[] = {50, 44, 39};
  int wraps = 0;
  const auto fit = sleepquote::chooseFit(heights, 3, [&](int, int) {
    wraps++;
    return 3;
  });
  EXPECT_EQ(fit.size, 0);
  EXPECT_FALSE(fit.cut);
  EXPECT_EQ(wraps, 1);
}

TEST(SleepQuoteFit, ExactlyFullAtEighteenStillFits) {
  const int heights[] = {50, 44, 39};
  const auto fit = sleepquote::chooseFit(heights, 3, [](int, int limit) { return limit; });
  EXPECT_EQ(fit.size, 0);
  EXPECT_FALSE(fit.cut);
}

TEST(SleepQuoteFit, StepsDownUntilTheWholeQuoteFits) {
  const int heights[] = {50, 44, 39};
  const int at16[] = {9, 9, 0};
  auto fit = sleepquote::chooseFit(heights, 3, [&](int size, int) { return at16[size]; });
  EXPECT_EQ(fit.size, 1);
  EXPECT_FALSE(fit.cut);

  const int at14[] = {12, 11, 10};
  fit = sleepquote::chooseFit(heights, 3, [&](int size, int) { return at14[size]; });
  EXPECT_EQ(fit.size, 2);
  EXPECT_FALSE(fit.cut);
}

TEST(SleepQuoteFit, TooLongEvenAtFourteenIsCutThere) {
  const int heights[] = {50, 44, 39};
  // The wrap is asked for limit + 1 lines, so an overflowing quote reports one more.
  const auto fit = sleepquote::chooseFit(heights, 3, [](int, int limit) { return limit + 1; });
  EXPECT_EQ(fit.size, 2);
  EXPECT_TRUE(fit.cut);
}

TEST(SleepQuotePlace, DropsTheDateFromTheListPlaceFormat) {
  EXPECT_EQ(sleepquote::placeFormat("Chương %d, trang %d, %02u/%02u/%04u"), "Chương %d, trang %d");
  EXPECT_EQ(sleepquote::placeFormat("Chapter %d, page %d, %02u/%02u/%04u"), "Chapter %d, page %d");
  EXPECT_EQ(sleepquote::placeFormat("第 %d 章第 %d 页，%02u/%02u/%04u"), "第 %d 章第 %d 页");
}

TEST(SleepQuotePlace, FormatWithoutDateIsRefused) {
  EXPECT_EQ(sleepquote::placeFormat("Chương %d"), "");
  EXPECT_EQ(sleepquote::placeFormat(nullptr), "");
}

TEST(SleepQuoteCut, BacksOffToAWholeWordThenCloses) {
  // The wrap cut "người" after "ngư" and put its own ellipsis there.
  const std::string line = "nghiệm, cũng vì lý do ngư\xe2\x80\xa6";
  const std::string closed = sleepquote::closeCutLine(line, 400, columns);
  EXPECT_EQ(closed, "nghiệm, cũng vì lý do\xe2\x80\xa6\xe2\x80\x9d");
}

TEST(SleepQuoteCut, DropsMoreWordsUntilTheClosingMarkFits) {
  const std::string line = "nghiệm, cũng vì lý do ngư\xe2\x80\xa6";
  const std::string closed = sleepquote::closeCutLine(line, 170, columns);
  EXPECT_LE(columns(closed), 170);
  EXPECT_EQ(closed, "nghiệm, cũng vì\xe2\x80\xa6\xe2\x80\x9d");
}

TEST(SleepQuoteCut, TextWithoutSpacesBacksOffByCodepoint) {
  const std::string line = "一二三四五\xe2\x80\xa6";
  const std::string closed = sleepquote::closeCutLine(line, 50, columns);
  EXPECT_EQ(closed, "一二三\xe2\x80\xa6\xe2\x80\x9d");
}

TEST(SleepQuoteTitle, EllipsisSitsOnTheLastWord) {
  std::string line = "những mùa gió và những con thuyền, \xe2\x80\xa6";
  sleepquote::tidyEllipsis(line);
  EXPECT_EQ(line, "những mùa gió và những con thuyền\xe2\x80\xa6");
  line = "kể theo lời bà ngoại\xe2\x80\xa6";
  sleepquote::tidyEllipsis(line);
  EXPECT_EQ(line, "kể theo lời bà ngoại\xe2\x80\xa6");
  line = "Sách thử có bìa";
  sleepquote::tidyEllipsis(line);
  EXPECT_EQ(line, "Sách thử có bìa");
}

// The cover of the quote's book is made at sleep when none is cached. A failure is kept as
// "<cover>.fail" so an unreadable cover costs one attempt, not one per sleep; that is right
// only when the generator had the heap it needs. The JPEG decoder refuses below 52 KB free
// (JpegToBmpConverter.cpp), so a failure while the heap was short says nothing about the
// book and must leave no marker.
TEST(SleepQuoteCover, HeapClearlyEnoughIsAboveTheDecoderFloor) {
  EXPECT_GT(sleepquote::COVER_MIN_FREE_HEAP, 52u * 1024u);
  EXPECT_TRUE(sleepquote::coverHeapReady({sleepquote::COVER_MIN_FREE_HEAP, sleepquote::COVER_MIN_BLOCK}));
  EXPECT_FALSE(sleepquote::coverHeapReady({sleepquote::COVER_MIN_FREE_HEAP - 1, sleepquote::COVER_MIN_BLOCK}));
  EXPECT_FALSE(sleepquote::coverHeapReady({sleepquote::COVER_MIN_FREE_HEAP, sleepquote::COVER_MIN_BLOCK - 1}));
}

TEST(SleepQuoteCover, FailureIsKeptOnlyWhenTheHeapWasEnoughAroundIt) {
  const sleepquote::HeapSample plenty{200u * 1024u, 100u * 1024u};
  // The metadata cache the check before it did not count took the heap under the floor.
  const sleepquote::HeapSample dipped{50u * 1024u, 40u * 1024u};
  EXPECT_TRUE(sleepquote::keepCoverFailure(plenty, plenty));
  EXPECT_FALSE(sleepquote::keepCoverFailure(dipped, plenty));
  EXPECT_FALSE(sleepquote::keepCoverFailure(plenty, dipped));
  EXPECT_FALSE(sleepquote::keepCoverFailure(dipped, dipped));
  // Plenty free but fragmented: the decoder's single blocks could not be had.
  EXPECT_FALSE(sleepquote::keepCoverFailure(plenty, {200u * 1024u, 16u * 1024u}));
}
