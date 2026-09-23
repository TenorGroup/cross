// Layout of the two Quotes list screens: the plain list (mockups A1-A4) and the list of
// books it shares a page style with (mockups B1, B2). Checked here because a wrong gap
// only shows on the panel as "reads badly", or worse, as a real mockup bug: a number
// crowding the hanging quote mark beside it.
#include <cassert>
#include <cstdio>

#include "components/QuoteListLayout.h"

namespace {

// X3 panel, portrait, with the header and the button hints already taken off the band
// (the same convention test/ui_layout/quote_blocks.cpp uses).
constexpr int16_t BAND_X = 0;
constexpr int16_t BAND_WIDTH = 528;
constexpr int16_t BAND_TOP = 0;
constexpr int16_t BAND_BOTTOM = 640;

// Noto Serif 14, Geist Bold 12, and Be Vietnam Pro 8, as the generated fonts measure at
// 150 DPI (ascent + descent from the .ttf, the same figures fontIds.h's fonts resolve to).
constexpr int16_t BODY_LINE = 40;
constexpr int16_t NUMBER_LINE = 34;
constexpr int16_t SMALL_LINE = 22;

// Real widths at those sizes (PIL textlength against the shipped .ttf), used instead of
// guesses so the number-box test reproduces the actual bug: a book past its ninth quote.
constexpr int16_t QUOTE_GLYPH_WIDTH = 13;   // '"' in Noto Serif 14
constexpr int16_t NUMBER_WIDTH_1 = 11;      // "1" in Geist Bold 12
constexpr int16_t NUMBER_WIDTH_99 = 32;     // "99" in Geist Bold 12

quotelist::Metrics metrics() { return {BAND_X, BAND_WIDTH, BAND_TOP, BAND_BOTTOM, BODY_LINE, NUMBER_LINE, SMALL_LINE}; }

// The real band of the Quotes list at each UI text size, as the simulator logs it ("Quote
// page band"): the header's bottom and the band's bottom (the footer line less its gap). Line
// heights are the tiers' own (components/UIScale.h): the caption, subtitle and body faces;
// the quote itself stays Noto Serif 14 (40) at every size.
struct Tier {
  const char* name;
  int16_t headerBottom, bandBottom;
  int16_t caption, subtitle, body;
};
constexpr Tier TIERS[] = {
    {"Small", 53, 741, 21, 26, 33},
    {"Medium", 58, 731, 26, 33, 38},
    {"Large", 63, 717, 33, 38, 43},
};

// Top of the first block: under the top row, which is set in the subtitle face.
int16_t firstBlockTop(const Tier& tier) {
  const quotelist::Metrics top{BAND_X, BAND_WIDTH, tier.headerBottom, tier.bandBottom, BODY_LINE, tier.body,
                               tier.subtitle};
  return quotelist::contentTop(top);
}

// The page size the screen uses for the tallest block of one view, then a check that that
// many such blocks, placed as the screen places them, keep every line above the band's
// bottom, and that one more would not have: the page is as full as it can be.
int checkTier(const Tier& tier, const bool allQuotes) {
  // All quotes: a bold title line in the subtitle face over the place line. One book: the
  // place line alone, in the caption face.
  const quotelist::Metrics m{BAND_X,    BAND_WIDTH, tier.headerBottom, tier.bandBottom,
                             BODY_LINE, tier.body,  allQuotes ? tier.subtitle : tier.caption};
  const int16_t top = firstBlockTop(tier);
  const int perPage = quotelist::blocksPerPage(m, top, allQuotes);
  if (perPage < 1 || perPage > quotelist::BLOCKS_PER_PAGE) {
    printf("FAIL: %s %s: %d blocks a page\n", tier.name, allQuotes ? "all" : "book", perPage);
    assert(false);
  }
  int16_t y = top;
  int16_t inkBottom = 0;
  for (int i = 0; i < perPage; ++i) {
    const auto block = quotelist::place(m, y, quotelist::MAX_BODY_LINES, allQuotes, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH);
    inkBottom = static_cast<int16_t>(block.sourceY + block.sourceStep + m.smallLineHeight);
    y = static_cast<int16_t>(y + block.height);
  }
  if (inkBottom > quotelist::footerY(m)) {
    printf("FAIL: %s %s: %d blocks end at %d, past the band's bottom %d\n", tier.name, allQuotes ? "all" : "book",
           perPage, inkBottom, quotelist::footerY(m));
    assert(false);
  }
  if (perPage < quotelist::BLOCKS_PER_PAGE) {
    const auto extra = quotelist::place(m, y, quotelist::MAX_BODY_LINES, allQuotes, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH);
    assert(extra.sourceY + extra.sourceStep + m.smallLineHeight > quotelist::footerY(m));
  }
  return perPage;
}

}  // namespace

int main() {
  const auto m = metrics();

  // Three to a screen: two quotes make one page, five make two, and the fifth (index 3,
  // the fourth quote) starts that second page.
  assert(quotelist::pageCount(2, 3) == 1);
  assert(quotelist::pageOf(0, 3) == 0);
  assert(quotelist::pageOf(1, 3) == 0);
  assert(quotelist::firstOfPage(0, 3) == 0);

  assert(quotelist::pageCount(5, 3) == 2);
  assert(quotelist::pageOf(2, 3) == 0);
  assert(quotelist::pageOf(3, 3) == 1);
  assert(quotelist::firstOfPage(1, 3) == 3);
  assert(quotelist::clampPage(5, 5, 3) == 1);
  assert(quotelist::clampPage(-1, 5, 3) == 0);
  // Two to a screen, as the larger text sizes page: five quotes make three pages, and the
  // fifth starts the third.
  assert(quotelist::pageCount(5, 2) == 3);
  assert(quotelist::pageOf(4, 2) == 2);
  assert(quotelist::firstOfPage(2, 2) == 4);

  // The sort row is the cursor's first stop, above every real row; the first block starts
  // below its divider and the breathing room under it.
  const auto sr = quotelist::sortRow(m);
  assert(sr.y == BAND_TOP + quotelist::SORT_ROW_PAD);
  assert(sr.height == SMALL_LINE + 2 * quotelist::SORT_ROW_PAD);
  assert(sr.dividerY == BAND_TOP + sr.height);
  assert(quotelist::contentTop(m) == sr.dividerY + quotelist::SORT_DIVIDER_GAP);

  // Three full (3-line, two-source-line) blocks, the tallest shape a page ever holds,
  // must not overlap each other or run past the footer/hint band.
  int16_t y = quotelist::contentTop(m);
  quotelist::Block blocks[3];
  for (auto& block : blocks) {
    block = quotelist::place(m, y, quotelist::MAX_BODY_LINES, /*twoSourceLines=*/true, NUMBER_WIDTH_1,
                             QUOTE_GLYPH_WIDTH);
    y = static_cast<int16_t>(y + block.height);
  }
  for (int i = 0; i < 3; ++i) {
    // Each block's own ink (quote lines through its last source line) fits before the
    // next block starts.
    const int16_t inkBottom =
        static_cast<int16_t>(blocks[i].sourceY + blocks[i].sourceStep + SMALL_LINE);
    assert(inkBottom <= blocks[i].textY + blocks[i].height);
    if (i + 1 < 3) assert(inkBottom <= blocks[i + 1].textY);
  }
  // The last block's own ink (not its trailing gap, which hangs off the bottom the way
  // components/QuoteBlockLayout.h's does) fits above the footer/hint band.
  const int16_t lastInkBottom =
      static_cast<int16_t>(blocks[2].sourceY + blocks[2].sourceStep + SMALL_LINE);
  assert(lastInkBottom <= quotelist::footerY(m));
  assert(quotelist::footerY(m) == BAND_BOTTOM);

  // The real mockup bug: a fixed number box, sized for one digit, crowded the hanging
  // quote once a book passed its ninth quote. Sized to the actual number text, "99"
  // still keeps the brand-floor gap.
  const auto wide = quotelist::place(m, 0, 2, false, NUMBER_WIDTH_99, QUOTE_GLYPH_WIDTH);
  const int16_t numberBoxRight = static_cast<int16_t>(wide.numberBoxX + wide.numberBoxWidth);
  assert(wide.hangingQuoteX - numberBoxRight >= quotelist::NUMBER_QUOTE_GAP);
  // And a single digit is never stretched to fill unused room either.
  const auto narrow = quotelist::place(m, 0, 2, false, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH);
  assert(narrow.numberBoxWidth < wide.numberBoxWidth);

  // One source line (inside a single book: place only) vs two (title, then place): the
  // second line costs exactly one small-font line step, nothing else moves.
  const auto oneLine = quotelist::place(m, 0, 2, false, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH);
  const auto twoLine = quotelist::place(m, 0, 2, true, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH);
  assert(oneLine.sourceStep == 0);
  assert(twoLine.sourceStep == SMALL_LINE);
  assert(oneLine.textX == twoLine.textX);
  assert(oneLine.sourceY == twoLine.sourceY);
  assert(twoLine.height - oneLine.height == SMALL_LINE);

  // Book list (mockup B1): a row per book, the count right-aligned, a date line under
  // the title, paging the same shape as the quote list's.
  const auto row = quotelist::bookRow(m, quotelist::contentTop(m));
  assert(row.x == BAND_X + quotelist::SIDE_INSET);
  assert(row.width == BAND_WIDTH - 2 * quotelist::SIDE_INSET);
  assert(row.countRightX == row.x + row.width - quotelist::BOOK_ROW_PAD);
  assert(row.dateY > row.titleY);
  assert(row.dateY - row.titleY == NUMBER_LINE);

  const int rowsPerPage = quotelist::bookRowsPerPage(m);
  assert(rowsPerPage >= 1);
  assert(quotelist::bookPageCount(rowsPerPage * 3, rowsPerPage) == 3);
  assert(quotelist::bookPageOf(rowsPerPage, rowsPerPage) == 1);
  assert(quotelist::bookFirstOfPage(1, rowsPerPage) == rowsPerPage);
  assert(quotelist::bookPageCount(0, rowsPerPage) == 1);

  // Every UI text size: the tallest blocks of both views stay above the footer. The smallest
  // size keeps the mockup's three to a page.
  for (const auto& tier : TIERS) {
    const int all = checkTier(tier, true);
    const int book = checkTier(tier, false);
    printf("%s: %d blocks a page for all quotes, %d inside one book\n", tier.name, all, book);
  }
  assert(checkTier(TIERS[0], true) == 3 && checkTier(TIERS[0], false) == 3);

  puts("PASS: quote list keeps the number box off the hanging quote, fits its pages to the band at "
       "every text size, and the book list pages the same way");
}
