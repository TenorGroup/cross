#pragma once
#include <algorithm>

struct OptionPopupWindow { int first; int count; bool paged; };
inline OptionPopupWindow optionPopupWindow(int total, int selected, int availableHeight, int headerHeight, int rowStride) {
  if (total <= 0 || rowStride <= 0) return {0, 0, false};
  const int slots = std::max(1, std::min(16, (availableHeight - headerHeight) / rowStride));
  const bool paged = total > slots;
  const int count = std::min(total, std::max(1, slots - (paged ? 2 : 0)));
  selected = std::max(0, std::min(selected, total - 1));
  const int first = selected / count * count;
  return {first, std::min(count, total - first), paged};
}
