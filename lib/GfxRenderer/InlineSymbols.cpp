#include "InlineSymbols.h"

#include "InlineSymbolBitmaps.h"

#include <algorithm>
#include <cstring>
namespace inlineSymbols {
namespace {
Resolver resolve = nullptr;
FontFilter accepts = nullptr;
ShapePainter shapePainter = nullptr;
bool (*painterEnabled)() = nullptr;
int handwrittenSymbol(const unsigned char* bytes) {
  if (bytes[0] != 0xE2 || !bytes[1] || !bytes[2]) return -1;
  if (bytes[1] == 0x86 && bytes[2] >= 0x90 && bytes[2] <= 0x93) {
    constexpr int arrows[] = {4, 2, 5, 3};
    return arrows[bytes[2] - 0x90];
  }
  if (bytes[1] == 0x9C && bytes[2] == 0x93) return 0;
  if (bytes[1] == 0x98 && bytes[2] == 0x85) return 6;
  if ((bytes[1] == 0x99 && bytes[2] == 0xA5) || (bytes[1] == 0x9D && bytes[2] == 0xA4)) return 10;
  return -1;
}
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
const inlineSymbolBitmaps::Glyph& chevronGlyph(Shape shape, int size) {
  using namespace inlineSymbolBitmaps;
  if (size < 12) {
    if (shape == Shape::Up) return kUpNet8;
    if (shape == Shape::Down) return kDownNet8;
    if (shape == Shape::Left) return kLeftNet8;
    return kRightNet8;
  }
  if (shape == Shape::Up) return size >= 16 ? kUpNet18 : kUpNet14;
  if (shape == Shape::Down) return size >= 16 ? kDownNet18 : kDownNet14;
  if (shape == Shape::Left) return size >= 16 ? kLeftNet18 : kLeftNet14;
  return size >= 16 ? kRightNet18 : kRightNet14;
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
void configurePainter(ShapePainter painter, bool (*enabled)()) {
  shapePainter = painter;
  painterEnabled = enabled;
}
void drawMarginPin(const GfxRenderer& r, int top) {
  const int size = marginPinHeight();
  if (shapePainter && shapePainter(r, static_cast<int>(Shape::MarginPin), 17, top + size / 2, size + 4, true)) return;
  // Keep the right edge at column 21, clear of the row's ring.
  constexpr unsigned rows[] = {0x0C6, 0x1EF, 0x1FF, 0x1FF, 0x0FE, 0x07C, 0x038, 0x010};
  const int height = marginPinHeight(), width = height == 8 ? 9 : 17;
  for (int dy = 0; dy < height; ++dy)
    for (int dx = 0; dx < width; ++dx)
      if (rows[dy * 8 / height] & (1u << (8 - dx * 9 / width)))
        r.drawPixel(22 - width + dx, top + 1 + dy, true);
}
int marginPinHeight() {
  return resolve ? resolve(10).marginPinHeight : 8;
}
int markTopOnCapitals(const GfxRenderer& r, const int font, const int y, const int height) {
  const int capTop = r.getTextInkTop(font, "H", EpdFontFamily::REGULAR);
  return y + (capTop + r.getFontAscenderSize(font) - height + 1) / 2;
}
void drawShape(const GfxRenderer& r, Shape shape, int x, int y, int size, bool black) {
  if (shapePainter && shapePainter(r, static_cast<int>(shape), x, y, size, black)) return;
  const int h = std::max(3, size / 2);
  if (shape == Shape::Up || shape == Shape::Down || shape == Shape::Left || shape == Shape::Right) {
    const auto& glyph = chevronGlyph(shape, size);
    drawGlyph(r, glyph, x, y, glyph.height / 2, black);
    return;
  }
  if (shape == Shape::Back) {
    const auto& glyph = backGlyph(size);
    drawGlyph(r, glyph, x, y, glyph.height / 2, black);
  } else if (shape == Shape::Select) {
    const auto& glyph = selectGlyph(size);
    drawGlyph(r, glyph, x, y, glyph.height / 2, black);
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
  if (!resolve || !accepts || !accepts(font) || !str) return -1;
  const bool handwritten = painterEnabled && painterEnabled();
  bool containsSymbol = strstr(str, "\xEE\x84") != nullptr;
  if (!containsSymbol && handwritten)
    for (const auto* bytes = reinterpret_cast<const unsigned char*>(str); *bytes; ++bytes)
      if (handwrittenSymbol(bytes) >= 0) { containsSymbol = true; break; }
  if (!containsSymbol) return -1;
  const auto* p = reinterpret_cast<const unsigned char*>(str);
  int advance = 0;
  while (*p) {
    const int alias = handwritten ? handwrittenSymbol(p) : -1;
    const int id = alias >= 0 ? alias : marker(p);
    if (id >= 0) {
      auto spec = resolve(id);
      if (alias >= 0) spec.label = nullptr;
      if (spec.shape == Shape::MarginPin && alias < 0) {
        if (draw) drawMarginPin(r, markTopOnCapitals(r, font, y, marginPinHeight()) - 1);
      } else if (spec.label) {
        if (draw) r.drawText(font, x + advance, y, spec.label, black, style);
        advance += r.getTextAdvanceX(font, spec.label, style);
      } else {
        const int size = spec.shape == Shape::Star ? 12 : std::max(12, r.getTextHeight(font) * 3 / 4);
        const int inkWidth = handwritten ? size + 6 : std::max(size + 1, glyphWidth(spec.shape, size));
        const int gap = std::max(5, r.getTextAdvanceX(font, " ", style));
        if (draw)
          drawShape(r, spec.shape, x + advance + inkWidth / 2 + gap, y + r.getFontAscenderSize(font) - size / 2 - 2, size,
                    black);
        advance += inkWidth + 2 * gap;
      }
      p += 3;
    } else {
      char chunk[192];
      size_t n = 0;
      while (*p && marker(p) < 0 && (!handwritten || handwrittenSymbol(p) < 0)) {
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
  if (shape == Shape::Up || shape == Shape::Down || shape == Shape::Left || shape == Shape::Right)
    return chevronGlyph(shape, size).width;
  return 0;
}
}  // namespace inlineSymbols
