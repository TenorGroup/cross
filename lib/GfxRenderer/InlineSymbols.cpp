#include "InlineSymbols.h"

#include <algorithm>
#include <cstring>
namespace inlineSymbols {
namespace {
Resolver resolve = nullptr;
FontFilter accepts = nullptr;
int marker(const unsigned char* p) {
  return p[0] == 0xEE && p[1] == 0x84 && p[2] >= 0x80 && p[2] < 0x90 ? p[2] - 0x80 : -1;
}
}  // namespace
void configure(Resolver resolver, FontFilter filter) {
  resolve = resolver;
  accepts = filter;
}
void drawShape(const GfxRenderer& r, Shape shape, int x, int y, int size, bool black) {
  const int h = std::max(3, size / 2);
  if (shape == Shape::Up || shape == Shape::Down) {
    const int dir = shape == Shape::Up ? -1 : 1;
    for (int row = 0; row <= h * 2; ++row) {
      const int half = row * h / (h * 2);
      r.drawLine(x - half, y + dir * (h - row), x + half, y + dir * (h - row), black);
    }
  } else if (shape == Shape::Left || shape == Shape::Right || shape == Shape::Back) {
    const int dir = shape == Shape::Right ? 1 : -1;
    const int n = shape == Shape::Back ? 2 : 1;
    const int half = n == 2 ? std::max(2, h / 2) : h;
    for (int i = 0; i < n; ++i)
      for (int col = 0; col <= half * 2; ++col) {
        const int cx = x + (n == 2 ? (i == 0 ? -half - 1 : half + 1) : 0);
        const int dy = col * h / (half * 2);
        const int px = cx + dir * (half - col);
        r.drawLine(px, y - dy, px, y + dy, black);
      }
  } else if (shape == Shape::Select) {
    const int xs[] = {x - h, x - h * 2 / 3, x - h / 4, x + h * 3 / 4, x + h, x - h / 4};
    const int ys[] = {y, y - h / 3, y + h / 6, y - h, y - h * 2 / 3, y + h * 3 / 4};
    r.fillPolygon(xs, ys, 6, black);
  } else if (shape == Shape::Erase) {
    r.drawLine(x - h, y, x - h / 2, y - h, black);
    r.drawLine(x - h, y, x - h / 2, y + h, black);
    r.drawLine(x - h / 2, y - h, x + h, y - h, black);
    r.drawLine(x - h / 2, y + h, x + h, y + h, black);
    r.drawLine(x + h, y - h, x + h, y + h, black);
    r.drawLine(x - h / 4, y - h / 3, x + h / 2, y + h / 3, black);
    r.drawLine(x - h / 4, y + h / 3, x + h / 2, y - h / 3, black);
  } else if (shape == Shape::Star) {
    const int xs[] = {x, x + h * 3 / 10, x + h,          x + h * 4 / 10, x + h * 6 / 10,
                      x, x - h * 6 / 10, x - h * 4 / 10, x - h,          x - h * 3 / 10};
    const int ys[] = {y - h,     y - h * 3 / 10, y - h * 3 / 10, y + h / 6,      y + h,
                      y + h / 2, y + h,          y + h / 6,      y - h * 3 / 10, y - h * 3 / 10};
    r.fillPolygon(xs, ys, 10, black);
  }
}
int text(const GfxRenderer& r, int font, int x, int y, const char* str, bool draw, bool black,
         EpdFontFamily::Style style, int spacing) {
  if (!resolve || !accepts || !accepts(font) || !str || !strstr(str, "\xEE\x84")) return -1;
  const auto* p = reinterpret_cast<const unsigned char*>(str);
  int advance = 0;
  while (*p) {
    const int id = marker(p);
    if (id >= 0) {
      const auto spec = resolve(id);
      if (spec.shape == Shape::MarginStar) {
        if (draw) {
          // Nine pixel star stays legible at the margin, clear of side arrows.
          constexpr unsigned rows[] = {0x010, 0x010, 0x038, 0x1FF, 0x0FE, 0x07C, 0x06C, 0x0C6, 0x082};
          const int top = y + r.getFontAscenderSize(font) - 10;
          for (int dy = 0; dy < 9; ++dy)
            for (int dx = 0; dx < 9; ++dx)
              if (rows[dy] & (1u << (8 - dx))) r.drawPixel(13 + dx, top + dy, true);
        }
      } else if (spec.label) {
        if (draw) r.drawText(font, x + advance, y, spec.label, black, style);
        advance += r.getTextAdvanceX(font, spec.label, style);
      } else {
        const int size = spec.shape == Shape::Star ? 12 : std::max(12, r.getTextHeight(font) * 3 / 4);
        if (draw)
          drawShape(r, spec.shape, x + advance + size / 2 + 5, y + r.getFontAscenderSize(font) - size / 2 - 2, size,
                    black);
        advance += size + 10;
      }
      p += 3;
    } else {
      char chunk[192];
      size_t n = 0;
      while (*p && marker(p) < 0) {
        const int bytes = *p < 0x80 ? 1 : (*p < 0xE0 ? 2 : (*p < 0xF0 ? 3 : 4));
        if (n + bytes >= sizeof(chunk)) break;
        for (int i = 0; i < bytes && *p; ++i) chunk[n++] = *p++;
      }
      chunk[n] = 0;
      if (draw) r.drawText(font, x + advance, y, chunk, black, style, BidiUtils::BidiBaseDir::AUTO, spacing);
      advance += r.getTextAdvanceX(font, chunk, style);
      if (spacing)
        for (const unsigned char* q = reinterpret_cast<const unsigned char*>(chunk); *q; ++q)
          if ((*q & 0xC0) != 0x80) advance += spacing;
    }
  }
  return advance;
}
}  // namespace inlineSymbols
