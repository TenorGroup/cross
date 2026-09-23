#pragma once
#include <cstdint>

// Shape of the two Quotes list screens: the plain list (mockups A1-A4: a number column, a
// quote hanging an opening mark into its own gutter, one or two lines naming the source)
// and the per-book list they share a page style with (mockups B1 the list of books, B2 the
// list inside one book).
//
// Pure arithmetic, no renderer, so the whole layout is checked on a desktop
// (test/ui_layout/quote_list.cpp) instead of by eye on the panel.
namespace quotelist {

// Left edge of the number column and the right margin of the band: the same inset the
// quote blocks this screen replaces used (components/QuoteBlockLayout.h).
constexpr int16_t SIDE_INSET = 20;
constexpr int16_t RIGHT_INSET = 24;
// Left edge of the quote body and the source lines under it. The number column and the
// hanging opening quote both live in the gutter to its left, between SIDE_INSET and TEXT_X.
constexpr int16_t TEXT_X = 80;

// A screen holds at most three quote blocks (the mockup's page at the smallest UI text size).
// How many lines each previews is not fixed: it is the most that lets the page's blocks fill the
// band (pageShape()), so long quotes end at the footer instead of leaving a strip of white above
// it. A page drops to fewer blocks before a preview drops under MIN_BODY_LINES. The full quote
// is one Select away.
constexpr int BLOCKS_PER_PAGE = 3;
constexpr int MIN_BODY_LINES = 3;
constexpr int MAX_BODY_LINES = 6;

// Gap from the quote's last line to its first source line.
constexpr int16_t SOURCE_GAP = 10;
// Least room between one block's last line and the next block's first, the rule in its middle.
// Room a full page leaves over is shared out evenly on top of it (pageShape()).
constexpr int16_t MIN_BLOCK_GAP = 20;
// Breathing room above and below the rule under a book row (the research note's own figure:
// "26 px trên và dưới gạch").
constexpr int16_t DIVIDER_GAP = 26;

// Horizontal padding each side of the number, inside its box.
constexpr int16_t NUMBER_BOX_PAD = 8;
constexpr int16_t NUMBER_BOX_MIN_WIDTH = 28;
// The floor a real mockup once broke: sized to only the usual one or two digits, the
// number box crowded the hanging opening quote once a book passed its ninth or ninety
// ninth quote. The box now sizes to the caller's own measurement of the number text, and
// this is the closest it may come to the quote mark beside it.
constexpr int16_t NUMBER_QUOTE_GAP = 4;

// Vertical padding around the "Sort:"/"View:" row's own text, and the gap from its
// divider down to the first block.
constexpr int16_t SORT_ROW_PAD = 6;
constexpr int16_t SORT_DIVIDER_GAP = 22;

struct Metrics {
  int16_t bandX = 0;
  int16_t bandWidth = 0;
  int16_t bandTop = 0;
  int16_t bandBottom = 0;
  int16_t bodyLineHeight = 0;    // Noto Serif 14, the quote's own font
  int16_t numberLineHeight = 0;  // the bold number column font (also the book title/count)
  int16_t smallLineHeight = 0;   // the small UI tier: sort row, source lines, footer, dates
};

// The "Sort: Newest" / "View: By book" row. It is the first cursor stop above the list
// (index -1; block/book row indices stay 0-based), so Confirm on it can cycle the sort or
// view without sharing an index with a real row.
struct SortRow {
  int16_t x = 0;
  int16_t y = 0;       // top of the row's own text
  int16_t width = 0;
  int16_t height = 0;  // fill this rect when the row is the cursor's stop
  int16_t dividerY = 0;
};

SortRow sortRow(const Metrics& m);
// Top of the first block/book row: below the sort row's divider and its breathing room.
int16_t contentTop(const Metrics& m);

struct Block {
  int16_t numberBoxX = 0;
  int16_t numberBoxY = 0;
  int16_t numberBoxWidth = 0;
  int16_t numberBoxHeight = 0;
  int16_t hangingQuoteX = 0;  // x for the opening quote glyph, hanging left of textX
  int16_t textX = 0;
  int16_t textY = 0;      // top of the first body line
  int16_t lineStep = 0;   // step between body lines
  int16_t sourceY = 0;    // top of the first source line
  int16_t sourceStep = 0; // step to the second source line; 0 when the block shows only one
  int16_t dividerY = 0;   // y of the rule under this block
  int16_t height = 0;     // top of this block to top of the next one
};

// What one page of quote blocks holds, worked out from the band alone, before any quote is
// read or wrapped, so paging and numbering stay the same whatever the quotes on a page.
struct PageShape {
  int blocks = 1;                 // blocks a page holds
  int bodyLines = MIN_BODY_LINES; // most quote lines a block previews
  int16_t gap = MIN_BLOCK_GAP;    // last line of one block to the first of the next
};

// The most blocks (up to BLOCKS_PER_PAGE) that each still preview MIN_BODY_LINES, then the most
// lines each can preview; the room left over when every block is that tall goes evenly into the
// gaps between them, so a page of long quotes ends at the band's bottom. A page of short quotes
// keeps the same gaps and leaves its white at the foot.
PageShape pageShape(const Metrics& m, int16_t top, bool twoSourceLines);

// Height one block takes: `bodyLines` quote lines, one or two source lines, and the gap under it.
int16_t blockHeight(int bodyLines, bool twoSourceLines, const Metrics& m, int16_t gap);

// Place a block with `bodyLines` quote lines at top `y`, `gap` under it. `numberWidth` and
// `quoteWidth` are the caller's own measurements (renderer.getTextWidth) of the number text in
// the number font and the opening quote glyph in the body font.
Block place(const Metrics& m, int16_t y, int bodyLines, bool twoSourceLines, int16_t numberWidth,
            int16_t quoteWidth, int16_t gap);

// Paging over `count` blocks, `perPage` to a screen.
int pageCount(int count, int perPage);
int pageOf(int index, int perPage);
int firstOfPage(int page, int perPage);
int clampPage(int page, int count, int perPage);

// Baseline for the footer line ("N trích dẫn, M cuốn sách" or "Trang P/N").
int16_t footerY(const Metrics& m);

// ---- Book list (mockup B1): one row per book, title left, count right, a date line
// under the title. ----

constexpr int16_t BOOK_ROW_PAD = 12;

struct BookRow {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;  // fill this rect when the row is selected
  int16_t titleX = 0;
  int16_t titleY = 0;
  int16_t countRightX = 0;  // right edge to anchor-right the count against
  int16_t countY = 0;
  int16_t dateX = 0;
  int16_t dateY = 0;
};

int16_t bookRowHeight(const Metrics& m);
BookRow bookRow(const Metrics& m, int16_t y);
// Rows a page holds: as many as fit between the sort row and the footer, at least one.
int bookRowsPerPage(const Metrics& m);
int bookPageCount(int count, int rowsPerPage);
int bookPageOf(int index, int rowsPerPage);
int bookFirstOfPage(int page, int rowsPerPage);

}  // namespace quotelist
