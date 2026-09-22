#include "QuoteListLayout.h"

namespace quotelist {
namespace {

int16_t sourceBlockHeight(const bool twoSourceLines, const Metrics& m) {
  return static_cast<int16_t>(twoSourceLines ? 2 * m.smallLineHeight : m.smallLineHeight);
}

// Bar-free ink height for a block: the quote's lines, the gap to the source, the source
// line(s). Does not include the trailing divider gap.
int16_t inkHeight(const int bodyLines, const bool twoSourceLines, const Metrics& m) {
  return static_cast<int16_t>(bodyLines * m.bodyLineHeight + SOURCE_GAP + sourceBlockHeight(twoSourceLines, m));
}

}  // namespace

SortRow sortRow(const Metrics& m) {
  SortRow row;
  row.x = static_cast<int16_t>(m.bandX + SIDE_INSET);
  row.y = static_cast<int16_t>(m.bandTop + SORT_ROW_PAD);
  row.width = static_cast<int16_t>(m.bandWidth - 2 * SIDE_INSET);
  row.height = static_cast<int16_t>(m.smallLineHeight + 2 * SORT_ROW_PAD);
  row.dividerY = static_cast<int16_t>(m.bandTop + row.height);
  return row;
}

int16_t contentTop(const Metrics& m) { return static_cast<int16_t>(sortRow(m).dividerY + SORT_DIVIDER_GAP); }

int16_t blockHeight(const int bodyLines, const bool twoSourceLines, const Metrics& m) {
  return static_cast<int16_t>(inkHeight(bodyLines, twoSourceLines, m) + DIVIDER_GAP);
}

Block place(const Metrics& m, const int16_t y, const int bodyLines, const bool twoSourceLines,
            const int16_t numberWidth, const int16_t quoteWidth) {
  Block block;
  block.numberBoxX = static_cast<int16_t>(m.bandX + SIDE_INSET);
  block.numberBoxY = y;
  block.numberBoxHeight = m.numberLineHeight;
  block.hangingQuoteX = static_cast<int16_t>(TEXT_X - quoteWidth);
  block.textX = TEXT_X;
  block.textY = y;
  block.lineStep = m.bodyLineHeight;
  block.sourceY = static_cast<int16_t>(y + bodyLines * m.bodyLineHeight + SOURCE_GAP);
  block.sourceStep = twoSourceLines ? m.smallLineHeight : int16_t{0};
  block.height = blockHeight(bodyLines, twoSourceLines, m);
  block.dividerY = static_cast<int16_t>(y + inkHeight(bodyLines, twoSourceLines, m) + DIVIDER_GAP / 2);

  // The number box sizes to the caller's own measurement of the number text instead of a
  // fixed width: a fixed box was the real mockup bug, crowding the hanging quote once a
  // book's numbering ran past a single digit. Padding is capped, never grown, so the box
  // still respects the floor between it and the hanging quote when the gutter is tight.
  const int16_t desiredWidth = static_cast<int16_t>(numberWidth + 2 * NUMBER_BOX_PAD);
  const int16_t maxWidth =
      static_cast<int16_t>(block.hangingQuoteX - NUMBER_QUOTE_GAP - block.numberBoxX);
  int16_t width = desiredWidth < maxWidth ? desiredWidth : maxWidth;
  if (width < numberWidth) width = numberWidth;
  if (width < NUMBER_BOX_MIN_WIDTH) width = NUMBER_BOX_MIN_WIDTH;
  block.numberBoxWidth = width;
  return block;
}

int pageCount(const int count) {
  if (count <= 0) return 1;
  return (count + BLOCKS_PER_PAGE - 1) / BLOCKS_PER_PAGE;
}

int pageOf(const int index) { return index < 0 ? 0 : index / BLOCKS_PER_PAGE; }

int firstOfPage(const int page) { return (page < 0 ? 0 : page) * BLOCKS_PER_PAGE; }

int clampPage(const int page, const int count) {
  const int last = pageCount(count) - 1;
  if (page < 0) return 0;
  return page > last ? last : page;
}

int16_t footerY(const Metrics& m) { return m.bandBottom; }

int16_t bookRowHeight(const Metrics& m) {
  return static_cast<int16_t>(m.numberLineHeight + m.smallLineHeight + BOOK_ROW_PAD + DIVIDER_GAP / 2);
}

BookRow bookRow(const Metrics& m, const int16_t y) {
  BookRow row;
  row.x = static_cast<int16_t>(m.bandX + SIDE_INSET);
  row.y = y;
  row.width = static_cast<int16_t>(m.bandWidth - 2 * SIDE_INSET);
  row.height = bookRowHeight(m);
  row.titleX = static_cast<int16_t>(row.x + BOOK_ROW_PAD);
  row.titleY = static_cast<int16_t>(y + BOOK_ROW_PAD / 2);
  row.countRightX = static_cast<int16_t>(row.x + row.width - BOOK_ROW_PAD);
  row.countY = row.titleY;
  row.dateX = row.titleX;
  row.dateY = static_cast<int16_t>(row.titleY + m.numberLineHeight);
  return row;
}

int bookRowsPerPage(const Metrics& m) {
  const int16_t rowHeight = bookRowHeight(m);
  const int available = m.bandBottom - contentTop(m);
  const int rows = rowHeight > 0 ? available / rowHeight : 0;
  return rows > 0 ? rows : 1;
}

int bookPageCount(const int count, const int rowsPerPage) {
  const int perPage = rowsPerPage > 0 ? rowsPerPage : 1;
  if (count <= 0) return 1;
  return (count + perPage - 1) / perPage;
}

int bookPageOf(const int index, const int rowsPerPage) {
  const int perPage = rowsPerPage > 0 ? rowsPerPage : 1;
  return index < 0 ? 0 : index / perPage;
}

int bookFirstOfPage(const int page, const int rowsPerPage) {
  const int perPage = rowsPerPage > 0 ? rowsPerPage : 1;
  return (page < 0 ? 0 : page) * perPage;
}

}  // namespace quotelist
