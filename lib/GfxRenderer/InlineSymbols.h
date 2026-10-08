#pragma once
#include <GfxRenderer.h>
namespace inlineSymbols {
enum class Shape { Select, Back, Up, Down, Left, Right, Star, MarginPin, Erase, None };
struct Spec {
  Shape shape;
  const char* label;
  int marginPinHeight = 8;
};
using Resolver = Spec (*)(int);
using FontFilter = bool (*)(int);
void configure(Resolver resolver, FontFilter filter);
// Returns -1 for ordinary text. PUA E100-E10F are UI-only inline controls.
int text(const GfxRenderer& renderer, int font, int x, int y, const char* text, bool draw, bool black,
         EpdFontFamily::Style style, int spacing = 0);
void drawShape(const GfxRenderer& renderer, Shape shape, int x, int y, int size, bool black);
// Dau ghim cua dong Yeu thich, ve o le trai (x 13), dinh o `top`.
void drawMarginPin(const GfxRenderer& renderer, int top);
int marginPinHeight();
// The top of a mark `height` px tall beside a line of `font` drawn at `y` (the y drawText takes): centred on the
// line's capitals, from their top to the baseline. Every pinned row's heart stands here (founder 07/10: the hearts
// sat low, on the middle of the line box).
int markTopOnCapitals(const GfxRenderer& renderer, int font, int y, int height);
// Pixel width of the bitmap glyph drawShape() picks for `shape` at target size `size`.
// Only meaningful for the bitmap-backed shapes (Back, Select); 0 otherwise.
int glyphWidth(Shape shape, int size);
}  // namespace inlineSymbols
