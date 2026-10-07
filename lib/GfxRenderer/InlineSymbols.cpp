#include "InlineSymbols.h"

#include "InlineSymbolBitmaps.h"

#include <algorithm>
#include <cstring>
namespace inlineSymbols {
namespace {
Resolver resolve = nullptr;
FontFilter accepts = nullptr;
int marker(const unsigned char* p) {
  return p[0] == 0xEE && p[1] == 0x84 && p[2] >= 0x80 && p[2] < 0x90 ? p[2] - 0x80 : -1;
}
// Back and Select are drawn from fixed reference bitmaps (see
// scripts/gen_back_arrow.py), not procedurally, so their pixels match the
// approved artwork exactly. Sizes below 16 use the net-14 glyph, everything
// else uses net-18; footer hints pass size=net (14 or 18) directly, and
// inline text sizes (max(12, textHeight*3/4)) fall on the same two tiers.
const inlineSymbolBitmaps::Glyph& backGlyph(int size) {
  return size >= 16 ? inlineSymbolBitmaps::kBackNet18 : inlineSymbolBitmaps::kBackNet14;
}
const inlineSymbolBitmaps::Glyph& selectGlyph(int size) {
  return size >= 16 ? inlineSymbolBitmaps::kSelectNet18 : inlineSymbolBitmaps::kSelectNet14;
}
// Centers the glyph horizontally on x and vertically on [y-h, y-h+height-1],
// so its top matches the top every other shape here uses (y-h).
void drawGlyph(const GfxRenderer& r, const inlineSymbolBitmaps::Glyph& glyph, int x, int y, int h, bool black) {
  const int x0 = x - glyph.width / 2;
  const int y0 = y - h;
  for (int row = 0; row < glyph.height; ++row) {
    const uint32_t bits = glyph.rows[row];
    for (int col = 0; col < glyph.width; ++col) {
      if (bits & (1u << (glyph.width - 1 - col))) r.drawPixel(x0 + col, y0 + row, black);
    }
  }
}
}  // namespace
void configure(Resolver resolver, FontFilter filter) {
  resolve = resolver;
  accepts = filter;
}
void drawMarginPin(const GfxRenderer& r, int top) {
  // Tim dac 9x8, cung o 9x9 cua ngoi sao cu, mui tim cham day o.
  constexpr unsigned rows[] = {0x0C6, 0x1EF, 0x1FF, 0x1FF, 0x0FE, 0x07C, 0x038, 0x010};
  for (int dy = 0; dy < 8; ++dy)
    for (int dx = 0; dx < 9; ++dx)
      if (rows[dy] & (1u << (8 - dx))) r.drawPixel(13 + dx, top + 1 + dy, true);
}
int markTopOnCapitals(const GfxRenderer& r, const int font, const int y, const int height) {
  const int capTop = r.getTextInkTop(font, "H", EpdFontFamily::REGULAR);
  return y + (capTop + r.getFontAscenderSize(font) - height + 1) / 2;
}
void drawShape(const GfxRenderer& r, Shape shape, int x, int y, int size, bool black) {
  const int h = std::max(3, size / 2);
  if (shape == Shape::Up || shape == Shape::Down) {
    const int dir = shape == Shape::Up ? -1 : 1;
    for (int row = 0; row <= h * 2; ++row) {
      const int half = row * h / (h * 2);
      r.drawLine(x - half, y + dir * (h - row), x + half, y + dir * (h - row), black);
    }
  } else if (shape == Shape::Left || shape == Shape::Right) {
    const int dir = shape == Shape::Right ? 1 : -1;
    for (int col = 0; col <= h * 2; ++col) {
      const int dy = col * h / (h * 2);
      const int px = x + dir * (h - col);
      r.drawLine(px, y - dy, px, y + dy, black);
    }
  } else if (shape == Shape::Back) {
    drawGlyph(r, backGlyph(size), x, y, h, black);
  } else if (shape == Shape::Select) {
    drawGlyph(r, selectGlyph(size), x, y, h, black);
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
      if (spec.shape == Shape::MarginPin) {
        // The heart's 8 rows start a row under the top drawMarginPin takes.
        if (draw) drawMarginPin(r, markTopOnCapitals(r, font, y, 8) - 1);
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
int glyphWidth(Shape shape, int size) {
  if (shape == Shape::Back) return backGlyph(size).width;
  if (shape == Shape::Select) return selectGlyph(size).width;
  return 0;
}
}  // namespace inlineSymbols
