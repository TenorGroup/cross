#pragma once

#include <algorithm>
#include <array>

#include "Epub/converters/DirectPixelWriter.h"

// Remove complete text bands while keeping cover art and the status/foot bands.
template <typename Renderer, typename Rect>
void clearContextMenuLines(const Renderer& renderer, Rect menu, const std::array<Rect, 4>& covers,
                           int contentTop, int contentBottom) {
  const int width = renderer.getScreenWidth();
  DirectPixelWriter pen;
  pen.init(const_cast<Renderer&>(renderer));
  const int rowBits = pen.displayWidthBytes * 8;
  auto spans = [&](int y, auto visit) {
    int x = 16;
    while (x < width - 16) {
      int next = width - 16;
      for (const auto& cover : covers) {
        if (cover.height <= 0 || y < cover.y - 7 || y >= cover.bottom() + 7) continue;
        if (x >= cover.x - 7 && x < cover.right() + 7) {
          x = cover.right() + 7;
          next = x;
          break;
        }
        if (cover.x - 7 > x) next = std::min(next, int(cover.x) - 7);
      }
      if (next > x) { visit(x, next - x); x = next; }
    }
  };
  auto hasInk = [&](int y) {
    bool ink = false;
    spans(y, [&](int x, int w) {
      for (int px = x; px < x + w; ++px) {
        const int bit = (pen.phyYBase + px * pen.phyYStepX + y * pen.phyYStepY) * rowBits +
                        pen.phyXBase + px * pen.phyXStepX + y * pen.phyXStepY;
        ink = ink || (pen.fb[bit >> 3] & (0x80 >> (bit & 7))) == 0;
      }
    });
    return ink;
  };
  int top = std::max(contentTop, int(menu.y) - 8);
  int bottom = std::min(contentBottom, int(menu.bottom()) + 8);
  // Accents and descenders can have a few empty raster rows inside a text line.
  int blank = 0;
  for (int y = top - 1; y >= contentTop && blank < 4; --y) {
    if (hasInk(y)) { top = y; blank = 0; }
    else ++blank;
  }
  blank = 0;
  for (int y = bottom; y < contentBottom && blank < 4; ++y) {
    if (hasInk(y)) { bottom = y + 1; blank = 0; }
    else ++blank;
  }
  for (int y = top; y < bottom; ++y)
    spans(y, [&](int x, int w) { renderer.fillRect(x, y, w, 1, false); });
}
