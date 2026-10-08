#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace readerstatus {

struct Cell {
  int x, width;
};
// Three disjoint lanes. Truncated text is measured again by the renderer before
// painting, so even an ellipsis wider than the cell cannot escape its lane.
inline Cell cell(int width, int index, int inset = 12, int gap = 8) {
  const int usable = std::max(0, width - inset * 2);
  const int start = inset + usable * index / 3;
  const int end = inset + usable * (index + 1) / 3;
  return {start + (index ? gap / 2 : 0), std::max(0, end - start - (index ? gap / 2 : 0) - (index < 2 ? gap / 2 : 0))};
}
inline int textX(Cell lane, int index, int measuredWidth) {
  return lane.x + (index == 0 ? 0 : index == 1 ? (lane.width - measuredWidth) / 2 : lane.width - measuredWidth);
}
inline void duration(char* out, size_t size, bool known, uint32_t seconds) {
  if (!known) { snprintf(out, size, "-"); return; }
  const uint64_t minutes = (static_cast<uint64_t>(seconds) + 59) / 60;
  if (minutes >= 60) snprintf(out, size, "%lluh %02llum", static_cast<unsigned long long>(minutes / 60),
                              static_cast<unsigned long long>(minutes % 60));
  else snprintf(out, size, "%llum", static_cast<unsigned long long>(minutes));
}

}  // namespace readerstatus
