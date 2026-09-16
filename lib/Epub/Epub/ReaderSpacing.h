#pragma once
#include <algorithm>
#include <cstdint>

namespace readerSpacing {
// Stable JSON values from textSpacingVersion 2: default, large, larger.
constexpr int paragraphGap(uint8_t mode, int lineHeight) {
  return mode == 1 ? lineHeight / 2 : mode == 2 ? lineHeight : std::clamp((lineHeight + 4) / 5, 2, 10);
}
// Narrow, default, wide. Explicit pixels match drawText and require no bitmap copy.
constexpr int letterPixels(uint8_t mode) { return mode == 0 ? -1 : mode == 2 ? 1 : 0; }
}  // namespace readerSpacing
