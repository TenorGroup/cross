#pragma once

#include <cstdint>
#include <EpdFontFamily.h>

class GfxRenderer {
 public:
  mutable uint8_t pixels[64 * 64]{};
  mutable unsigned outside = 0;
  void drawPixel(int x, int y, bool black = true) const {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) { ++outside; return; }
    pixels[y * 64 + x] = black;
  }
  void drawLine(int x0, int y0, int x1, int y1, bool black = true) const;
};

class MappedInputManager;
