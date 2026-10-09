#pragma once

namespace tenorchrome {
struct Rect {
  int y, height, width, edge;
  int x = 0;
  int bottom() const { return edge; }
};
struct Layout {
  Rect bodyRect, barRect;
};
inline bool x3ScreenLayoutEnabled() { return false; }
inline Layout x3ScreenLayout(const GfxRenderer& renderer, int = 0) {
  return {{0, 0, 528, renderer.getScreenHeight() - 122},
          {renderer.getScreenHeight() - 52, 60, 528, renderer.getScreenHeight()}};
}
inline int tipHeight(const GfxRenderer&, const char*) { return 0; }
}
