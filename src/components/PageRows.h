#pragma once
#include <algorithm>

// Rows shown a page at a time, `rows` to a page, with a line or button after them that turns or names the page
// ("page 1/2", "Next page »"). A last page of 1 row is never made (founder 06/10/2026): that row stands in the
// place of the line of the page before, which then turns and names no page. The one rule for every list paged so.
namespace pagerows {
inline int pageCount(const int count, const int rows) {
  if (rows <= 0 || count <= 0) return 1;
  const int pages = (count + rows - 1) / rows;
  return pages > 1 && count % rows == 1 ? pages - 1 : pages;
}
// The page a row falls on, and that page's first row.
inline int pageOf(const int row, const int rows, const int count) {
  return rows > 0 ? std::min(std::max(0, row) / rows, pageCount(count, rows) - 1) : 0;
}
inline int pageTop(const int row, const int rows, const int count) { return pageOf(row, rows, count) * rows; }
// The rows a page starting at `top` shows: `rows`, fewer on the last page, one more where the lone row took the line.
inline int rowsOnPage(const int top, const int rows, const int count) {
  const int left = std::max(0, count - top);
  return left == rows + 1 ? left : std::min(rows, left);
}
// Whether the page has the line that names or turns it: there are pages and no row stands in its place.
inline bool pageNamed(const int top, const int rows, const int count) {
  return pageCount(count, rows) > 1 && rowsOnPage(top, rows, count) <= rows;
}
}  // namespace pagerows
