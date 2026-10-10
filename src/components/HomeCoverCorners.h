#pragma once

#include "components/themes/TenorRadius.h"

// Painting page colour over the corners keeps the book art free of a border.
template <typename Renderer>
void homeCoverCorners(const Renderer& renderer, int x, int y, int width, int height) {
  renderer.maskRoundedRectOutsideCorners(x, y, width, height, tenorradius::cover(width));
}
