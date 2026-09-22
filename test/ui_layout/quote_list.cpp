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

}  // namespace

int main() {
  const auto m = metrics();

  // Three to a screen: two quotes make one page, five make two, and the fifth (index 3,
  // the fourth quote) starts that second page.
  assert(quotelist::pageCount(2) == 1);
  assert(quotelist::pageOf(0) == 0);
  assert(quotelist::pageOf(1) == 0);
  assert(quotelist::firstOfPage(0) == 0);

  assert(quotelist::pageCount(5) == 2);
  assert(quotelist::pageOf(2) == 0);
  assert(quotelist::pageOf(3) == 1);
  assert(quotelist::firstOfPage(1) == 3);
  assert(quotelist::clampPage(5, 5) == 1);
  assert(quotelist::clampPage(-1, 5) == 0);

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

  puts("PASS: quote list keeps the number box off the hanging quote, pages three at a time, "
       "and the book list pages the same way");
}
