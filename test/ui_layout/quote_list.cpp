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
// `place` is the face the place line under a quote takes (QuotesActivity::placeFont()): the
// subtitle face where a long place fits the text column in it, the caption face at the largest
// size, where it does not.
struct Tier {
  const char* name;
  int16_t headerBottom, bandBottom;
  int16_t caption, subtitle, body, place;
};
constexpr Tier TIERS[] = {
    {"Small", 53, 741, 21, 26, 33, 26},
    {"Medium", 58, 731, 26, 33, 38, 33},
    {"Large", 63, 717, 33, 38, 43, 33},
};

// Top of the first block: under the top row, which is set in the subtitle face.
int16_t firstBlockTop(const Tier& tier) {
  const quotelist::Metrics top{BAND_X, BAND_WIDTH, tier.headerBottom, tier.bandBottom, BODY_LINE, tier.body,
                               tier.subtitle};
  return quotelist::contentTop(top);
}

// The lines under a quote step by one height: the place face inside one book, the bold title's
// subtitle face in the all-quotes view, where the place line rides under the title.
quotelist::Metrics tierMetrics(const Tier& tier, const bool allQuotes) {
  return {BAND_X, BAND_WIDTH, tier.headerBottom, tier.bandBottom, BODY_LINE, tier.body,
          allQuotes ? tier.subtitle : tier.place};
}

int16_t inkBottom(const quotelist::Block& block, const quotelist::Metrics& m) {
  return static_cast<int16_t>(block.sourceY + block.sourceStep + m.smallLineHeight);
}

// The page shape of one view at one text size, then the page the screen draws when every block
// is as tall as the shape allows (a page of long quotes): the blocks keep clear of
// each other, the rule sits in the middle of every gap, the gaps are one size, and the last
// block ends at the band's bottom instead of leaving a strip of white above the footer.
quotelist::PageShape checkTier(const Tier& tier, const bool allQuotes) {
  const auto m = tierMetrics(tier, allQuotes);
  const int16_t top = firstBlockTop(tier);
  const auto shape = quotelist::pageShape(m, top, allQuotes);
  const char* view = allQuotes ? "all" : "book";
  printf("%s %s: %d blocks of up to %d lines, gap %d\n", tier.name, view, shape.blocks, shape.bodyLines, shape.gap);
  assert(shape.blocks >= 1 && shape.blocks <= quotelist::BLOCKS_PER_PAGE);
  assert(shape.bodyLines >= quotelist::MIN_BODY_LINES && shape.bodyLines <= quotelist::MAX_BODY_LINES);
  assert(shape.gap >= quotelist::MIN_BLOCK_GAP);

  int16_t y = top;
  int16_t lastInk = 0;
  int16_t previousInk = -1;
  for (int i = 0; i < shape.blocks; ++i) {
    const auto block =
        quotelist::place(m, y, shape.bodyLines, allQuotes, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH, shape.gap);
    if (previousInk >= 0) assert(block.textY - previousInk == shape.gap);  // one gap size down the page
    lastInk = inkBottom(block, m);
    assert(block.height == lastInk - block.textY + shape.gap);
    assert(block.dividerY - lastInk == shape.gap / 2);  // the rule halves its gap
    previousInk = lastInk;
    y = static_cast<int16_t>(y + block.height);
  }
  const int bottomGap = quotelist::footerY(m) - lastInk;
  if (bottomGap < 0 || bottomGap >= shape.blocks) {
    printf("FAIL: %s %s: a page of long quotes leaves %d px under its last block\n", tier.name, view, bottomGap);
    assert(false);
  }
  // As many lines as fit: one more line in every block would not fit at the smallest gap.
  if (shape.bodyLines < quotelist::MAX_BODY_LINES) {
    const auto taller = quotelist::blockHeight(shape.bodyLines + 1, allQuotes, m, quotelist::MIN_BLOCK_GAP);
    assert(top + shape.blocks * taller - quotelist::MIN_BLOCK_GAP > quotelist::footerY(m));
  }
  // As many blocks as fit: one more block of the shortest preview would not.
  if (shape.blocks < quotelist::BLOCKS_PER_PAGE) {
    const auto shortest = quotelist::blockHeight(quotelist::MIN_BODY_LINES, allQuotes, m, quotelist::MIN_BLOCK_GAP);
    assert(top + (shape.blocks + 1) * shortest - quotelist::MIN_BLOCK_GAP > quotelist::footerY(m));
  }
  return shape;
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

  // The real mockup bug: a fixed number box, sized for one digit, crowded the hanging
  // quote once a book passed its ninth quote. Sized to the actual number text, "99"
  // still keeps the brand-floor gap.
  const auto wide = quotelist::place(m, 0, 2, false, NUMBER_WIDTH_99, QUOTE_GLYPH_WIDTH, quotelist::MIN_BLOCK_GAP);
  const int16_t numberBoxRight = static_cast<int16_t>(wide.numberBoxX + wide.numberBoxWidth);
  assert(wide.hangingQuoteX - numberBoxRight >= quotelist::NUMBER_QUOTE_GAP);
  // And a single digit is never stretched to fill unused room either.
  const auto narrow = quotelist::place(m, 0, 2, false, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH, quotelist::MIN_BLOCK_GAP);
  assert(narrow.numberBoxWidth < wide.numberBoxWidth);

  // One source line (inside a single book: place only) vs two (title, then place): the
  // second line costs exactly one small-font line step, nothing else moves.
  const auto oneLine = quotelist::place(m, 0, 2, false, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH, quotelist::MIN_BLOCK_GAP);
  const auto twoLine = quotelist::place(m, 0, 2, true, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH, quotelist::MIN_BLOCK_GAP);
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

  // A short quote keeps the page's rhythm: the same gap under it as under a long one, so a page
  // of short quotes reads as the same list with white left at its foot.
  {
    const auto small = tierMetrics(TIERS[0], false);
    const auto shape = quotelist::pageShape(small, firstBlockTop(TIERS[0]), false);
    const auto shortBlock = quotelist::place(small, 0, 1, false, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH, shape.gap);
    const auto longBlock =
        quotelist::place(small, 0, shape.bodyLines, false, NUMBER_WIDTH_1, QUOTE_GLYPH_WIDTH, shape.gap);
    assert(shortBlock.height - inkBottom(shortBlock, small) == longBlock.height - inkBottom(longBlock, small));
  }

  // Every UI text size, both views: the page fills to the band's bottom with long quotes.
  for (const auto& tier : TIERS) {
    checkTier(tier, true);
    checkTier(tier, false);
  }
  // The reported screen, one book at the smallest size: three blocks of four lines, where the fixed
  // three-line blocks left 123 px of white above the footer.
  const auto book = checkTier(TIERS[0], false);
  assert(book.blocks == 3 && book.bodyLines == 4);
  // All quotes at the smallest size keeps the mockup's three blocks to a page.
  assert(checkTier(TIERS[0], true).blocks == 3);

  puts("PASS: quote list keeps the number box off the hanging quote, fills its pages to the band at "
       "every text size, and the book list pages the same way");
}
