#pragma once
// The pen of the tenor/ugly shell: baked Mansalva letters that jump by integers, circles and
// underlines drawn from fixed tables, hand-drawn battery and button hints. Everything is
// deterministic, so a screen drawn twice has the same pixels.
#include <GfxRenderer.h>

#include <cstdint>

class MappedInputManager;

namespace ugly {

enum class Size : uint8_t { S22, S30, S38, S52 };

// Registers the four fonts with the renderer once (takes the render lock, so call it from the main
// task and never while holding that lock).
void ensureFonts(GfxRenderer& renderer);

// Baseline-anchored text. Returns the advance. A string with a character the baked font lacks (a
// Chinese title, say) is drawn whole in the UI font instead, without jumps.
int text(const GfxRenderer& renderer, Size size, int x, int baseline, const char* utf8, bool black = true);
int width(const GfxRenderer& renderer, Size size, const char* utf8);
// Height of the ink above the baseline for a line at this size, for placing boxes.
int ascent(Size size);
// The first character of `utf8` shortened with an ellipsis so the result fits `maxWidth`.
std::string fit(const GfxRenderer& renderer, Size size, const std::string& utf8, int maxWidth);

// A plain sentence broken into lines of at most maxWidth, one under another. Returns the lines used.
int paragraph(const GfxRenderer& renderer, Size size, int x, int baseline, int maxWidth, int lineHeight, const char* utf8);

struct Box {
  int x0, y0, x1, y1;
};
enum class Circle : uint8_t { Word, Object, Row };
// A pen circle around a box, padded by padX and padY on each side.
void circle(const GfxRenderer& renderer, Circle role, const Box& box, int padX, int padY, int stroke = 3);
// A shaky underline from x0 to x1, a little uphill.
void underline(const GfxRenderer& renderer, int x0, int x1, int y, uint32_t seed, int stroke = 2);
// A shaky straight line (the margin of a notebook page).
void line(const GfxRenderer& renderer, int x0, int y0, int x1, int y1, uint32_t seed, int stroke = 1);
// A hand-drawn tick, its elbow at (x, y).
void tick(const GfxRenderer& renderer, int x, int y);
// A battery drawn by hand with its level as pen strokes, no percent.
void battery(const GfxRenderer& renderer, int x, int y, int percent);

struct Hints {
  bool back = false, confirm = false, left = false, right = false;
};
// The bottom bar: battery, the clock when the clock shows, and a hand-drawn symbol over each front
// button that does something on this screen.
void statusBar(const GfxRenderer& renderer, const MappedInputManager& input, Hints hints);

}  // namespace ugly
