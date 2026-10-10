#pragma once

#include "components/TenorMenuChrome.h"

namespace tenorchrome {
inline void drawDestructiveActionIcon(const GfxRenderer& renderer, int x, int y) {
  drawRoundRing(renderer, x, y, 24, 24, 12, 2, false);
  renderer.drawLine(x + 4, y + 4, x + 19, y + 19);
  renderer.drawLine(x + 4, y + 5, x + 18, y + 19);
}
}  // namespace tenorchrome
