#pragma once

#include "components/TenorMenuChrome.h"

namespace x3activity {
inline void drawBar(const GfxRenderer& renderer, const tenorchrome::X3ScreenRect, const char* title,
                    const char* = "") {
  tenorchrome::drawX3ActivityPath(renderer, title);
}
}
