#pragma once
#include <algorithm>

#include "components/PageRows.h"

struct OptionPopupWindow { int first; int count; bool paged; bool next; };
// The options of one page, between a "previous page" and a "next page" button when they do not all fit. A lone
// last option takes the place of the "next page" button of the page before (PageRows.h).
inline OptionPopupWindow optionPopupWindow(int total, int selected, int availableHeight, int headerHeight, int rowStride) {
  if (total <= 0 || rowStride <= 0) return {0, 0, false, false};
  const int slots = std::max(1, std::min(16, (availableHeight - headerHeight) / rowStride));
  const bool paged = total > slots;
  const int count = std::min(total, std::max(1, slots - (paged ? 2 : 0)));
  const int first = pagerows::pageTop(std::max(0, std::min(selected, total - 1)), count, total);
  const int shown = pagerows::rowsOnPage(first, count, total);
  return {first, shown, paged, paged && shown <= count};  // next: the page keeps its "next page" button
}
