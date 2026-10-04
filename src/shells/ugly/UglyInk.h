#pragma once
// The pen of the tenor/ugly shell: baked straight Mansalva letters, which the level "ugly af" turns, shrinks and
// lets jump as they are drawn, circles and underlines drawn from fixed tables, hand-drawn battery and button
// hints. Everything is deterministic, so a screen drawn twice has the same pixels.
#include <GfxRenderer.h>

#include <cstdint>

class MappedInputManager;

namespace ugly {

// The per-frame log lines are measurement. They exist in the probe build, and in the simulator where
// the tests read them; a release build carries neither the strings nor the heap query.
#if defined(TENOR_PRESS_PROBE) || defined(SIMULATOR)
#define UGLY_FRAME_LOG 1
#endif

enum class Size : uint8_t { S22, S30, S38, S52 };

// Registers the four fonts with the renderer once. It takes no lock: boot calls it under the lock it
// holds for the other fonts when the shell is in use, and the three screens and the sleep screen call
// it from onEnter, before they ask for a frame. Never call it from a draw that can overlap another.
void ensureFonts(GfxRenderer& renderer);

// Baseline-anchored text. Returns the advance. A string with a character the baked font lacks (a
// Chinese title, say) is drawn whole in the UI font instead, without jumps.
int text(const GfxRenderer& renderer, Size size, int x, int baseline, const char* utf8, bool black = true);
int width(const GfxRenderer& renderer, Size size, const char* utf8);
// Height of the ink above the baseline for a line at this size, for placing boxes.
int ascent(Size size);
// The first character of `utf8` shortened with an ellipsis so the result fits `maxWidth`.
std::string fit(const GfxRenderer& renderer, Size size, const std::string& utf8, int maxWidth);

// A plain sentence broken into lines of at most maxWidth, one under another. Returns the lines used (with draw false it only counts them).
int paragraph(const GfxRenderer& renderer, Size size, int x, int baseline, int maxWidth, int lineHeight, const char* utf8,
              bool draw = true);

// A chapter title written by hand: one to three lines centred between x0 and x1, in the largest pen
// size that fits the band from top to bottom, with a shaky underline under the last line. Returns
// false and draws nothing when no size fits, or when the baked font lacks a letter of the title (the
// heading in the book stays then). `draw` false only asks whether it would fit.
bool chapterTitle(const GfxRenderer& renderer, const char* utf8, int x0, int x1, int top, int bottom, bool draw);

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

// A hand-drawn mark, one pen stroke from a baked table, about 20 px across, centred on (cx, cy).
enum class Mark : uint8_t { Left, Right, Up, Down, Tick, Back };
void mark(const GfxRenderer& renderer, Mark m, int cx, int cy);
// The page next door on each side, over the lower corners: a drawn arrow beside a short name.
void pageHints(const GfxRenderer& renderer, const char* before, const char* after, int baseline);

// left and right are the front Left and Right buttons, which walk up and down the screen.
struct Hints {
  bool back = false, confirm = false, left = false, right = false;
};
// The bottom bar: battery, the clock when the clock shows, and a hand-drawn symbol over each front
// button that does something on this screen.
void statusBar(const GfxRenderer& renderer, const MappedInputManager& input, Hints hints);

}  // namespace ugly
