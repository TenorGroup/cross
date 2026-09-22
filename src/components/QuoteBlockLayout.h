#pragma once
#include <cstdint>

// Shape of the Quotes list: every saved quote is a block with a bar down its left side,
// the quote itself in the reading font beside the bar, and under it a smaller line naming
// the book, the place in it and the day it was kept.
//
// Pure arithmetic, no renderer, so the whole layout is checked on a desktop
// (test/ui_layout/quote_blocks.cpp) instead of by eye on the panel.
namespace quoteblock {

// Inset from the band edge to the bar, well past the eight-pixel floor every screen keeps.
constexpr int16_t SIDE_INSET = 20;
// The bar is the only mark on the page: a selected block widens it, and nothing else
// changes, so the list stays plain black on white.
constexpr int16_t BAR_WIDTH = 2;
constexpr int16_t BAR_WIDTH_SELECTED = 6;
// Air between the bar and the first glyph.
constexpr int16_t BAR_GAP = 14;
// Air between the quote and the line naming its source.
constexpr int16_t SOURCE_GAP = 8;
// Air between two blocks.
constexpr int16_t BLOCK_GAP = 24;
// Leading added to the reading font's own line height, so a block breathes the way a
// page of the book does.
constexpr int16_t LINE_EXTRA = 3;
// Longest preview one block shows. The whole quote is one Select away.
constexpr int MAX_LINES = 4;

struct Metrics {
  int16_t bandX = 0;
  int16_t bandWidth = 0;
  int16_t quoteLineHeight = 0;
  int16_t sourceLineHeight = 0;
};

struct Block {
  int16_t barX = 0;
  int16_t barY = 0;
  int16_t barWidth = 0;
  int16_t barHeight = 0;
  int16_t textX = 0;
  int16_t textY = 0;
  int16_t textWidth = 0;
  // Baseline box step between two quote lines.
  int16_t lineStep = 0;
  int16_t sourceY = 0;
  // Top of this block to top of the next one.
  int16_t height = 0;
};

// Height one block of `lines` quote lines takes, the gap below it included.
int16_t blockHeight(int lines, const Metrics& m);

// Place a block of `lines` quote lines with its top edge at `y`.
Block place(const Metrics& m, int16_t y, int lines, bool selected);

// First block to draw so block `selected` is on screen. Starts from the current `top` and
// moves it as little as the band allows, so scrolling down does not jump the page.
// `lines` holds the quote-line count of each of the `count` blocks.
int followTop(const uint8_t* lines, int count, int top, int selected, int16_t bandHeight, const Metrics& m);

}  // namespace quoteblock
