#pragma once
#include <algorithm>

inline int statsActionIndex(int row, bool paged) { return row - (paged ? 1 : 0); }
inline int statsSelectionForTier(int selected, bool wasPaged, bool paged) {
  if (selected <= 0 || wasPaged == paged) return selected;
  return std::max(1, selected + (paged ? 1 : -1));
}
