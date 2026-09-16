#pragma once
#include <GfxRenderer.h>
namespace inlineSymbols {
enum class Shape { Select, Back, Up, Down, Left, Right, Star, MarginStar, Erase, None };
struct Spec {
  Shape shape;
  const char* label;
};
using Resolver = Spec (*)(int);
using FontFilter = bool (*)(int);
void configure(Resolver resolver, FontFilter filter);
// Returns -1 for ordinary text. PUA E100-E10F are UI-only inline controls.
int text(const GfxRenderer& renderer, int font, int x, int y, const char* text, bool draw, bool black,
         EpdFontFamily::Style style, int spacing = 0);
void drawShape(const GfxRenderer& renderer, Shape shape, int x, int y, int size, bool black);
}  // namespace inlineSymbols
