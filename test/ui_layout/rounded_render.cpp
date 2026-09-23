// Every rounded block GfxRenderer draws, through the real renderer into a RAM framebuffer (the
// button_symbols recipe): fills, borders and cover masks all follow the one continuous-corner
// profile (lib/GfxRenderer/ContinuousCorner.h), a border is the outer block less the inner one
// set in by its width, and a corner not asked for stays square.
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "ContinuousCorner.h"

namespace {

int failures = 0;
void check(const bool condition, const char* what, const int a = 0, const int b = 0) {
  if (!condition) {
    std::printf("FAIL: %s (%d, %d)\n", what, a, b);
    ++failures;
  }
}

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
void clear(const uint8_t value = 0xff) { std::fill(pixels.begin(), pixels.end(), value); }
bool isInk(const int x, const int y) {
  const uint8_t byte = pixels[static_cast<size_t>(y) * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8];
  return ((byte >> (7 - (x % 8))) & 1) == 0;
}

// The expected ink of a filled rounded block, one row at a time: row i is set in on each side by
// the cut of the corner it belongs to (top rows from the top corners, bottom rows from the bottom).
struct Corners {
  bool tl, tr, bl, br;
};
bool expectedInk(const int w, const int h, const uint8_t* cut, const int rows, const Corners c, const int i,
                 const int j) {
  int left = 0, right = 0;
  if (i < rows) {
    if (c.tl) left = cut[i];
    if (c.tr) right = cut[i];
  }
  const int bottom = h - 1 - i;
  if (bottom < rows) {
    if (c.bl) left = std::max<int>(left, cut[bottom]);
    if (c.br) right = std::max<int>(right, cut[bottom]);
  }
  return j >= left && j < w - right;
}

void checkFill(const GfxRenderer& renderer, const int w, const int h, const int r, const Corners c) {
  clear();
  constexpr int X = 40, Y = 30;
  renderer.fillRoundedRect(X, Y, w, h, r, c.tl, c.tr, c.bl, c.br, Color::Black);
  const bool all = c.tl && c.tr && c.bl && c.br;
  uint8_t cut[continuouscorner::MAX_ROWS];
  const int rows = continuouscorner::profile(r, std::min(w, h) / (all ? 2 : 1), cut);
  int wrong = 0;
  for (int i = -2; i < h + 2; ++i)
    for (int j = -2; j < w + 2; ++j) {
      const bool inside = i >= 0 && j >= 0 && i < h && j < w && expectedInk(w, h, cut, rows, c, i, j);
      wrong += isInk(X + j, Y + i) != inside ? 1 : 0;
    }
  check(wrong == 0, "fillRoundedRect follows the profile, w/r", w * 100 + r, wrong);
}

}  // namespace

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

int main() {
  HalDisplay display;
  GfxRenderer renderer{display};
  renderer.begin();
  // Identity mapping (phyX = x, phyY = y), so the framebuffer reads in the drawing's own x and y.
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);

  // Fills: every corner, one side only (the Lyra tile's top pair), and sizes small enough that
  // the budget takes the smoothing and then the radius.
  for (const int r : {3, 6, 8, 12, 27}) {
    checkFill(renderer, 300, 66, r, {true, true, true, true});
    checkFill(renderer, 120, 40, r, {true, true, false, false});
    checkFill(renderer, 28, 33, r, {true, true, true, true});
  }
  checkFill(renderer, 9, 9, 8, {true, true, true, true});

  // A border is the outer block less the inner block set in by the line width, with the inner
  // corner concentric (radius r - width) and sized by the inner box, plus the few pixels that join
  // its rows into one line where the curve is nearly flat (rounded_border.cpp checks the line).
  // Nothing of it lies outside the outer block.
  for (const int r : {3, 8, 12, 27}) {
    for (const int lw : {1, 2, 3}) {
      constexpr int X = 50, Y = 40, W = 200, H = 90;
      clear();
      renderer.drawRoundedRect(X, Y, W, H, lw, r, true);
      std::vector<bool> drawn(W * H);
      for (int i = 0; i < H; ++i)
        for (int j = 0; j < W; ++j) drawn[i * W + j] = isInk(X + j, Y + i);
      clear();
      renderer.fillRoundedRect(X, Y, W, H, r, Color::Black);
      std::vector<bool> outerBlock(W * H);
      for (int i = 0; i < H; ++i)
        for (int j = 0; j < W; ++j) outerBlock[i * W + j] = isInk(X + j, Y + i);
      renderer.fillRoundedRect(X + lw, Y + lw, W - 2 * lw, H - 2 * lw, std::max(r - lw, 0), Color::White);
      int missing = 0, outside = 0, joins = 0;
      for (int i = 0; i < H; ++i)
        for (int j = 0; j < W; ++j) {
          const int n = i * W + j;
          const bool ring = isInk(X + j, Y + i);
          missing += ring && !drawn[n] ? 1 : 0;
          outside += drawn[n] && !outerBlock[n] ? 1 : 0;
          joins += drawn[n] && !ring ? 1 : 0;
        }
      check(missing == 0, "drawRoundedRect covers outer less inner, r/lw", r, lw);
      check(outside == 0, "drawRoundedRect stays inside the outer block, r/lw", r, lw);
      // The joins are a handful per corner, never a second line.
      check(joins <= 4 * 3 * lw, "drawRoundedRect joins stay few, r/lw", r * 100 + lw, joins);
    }
  }

  // A border with one rounded pair keeps the old contract: the corners not asked for are left
  // open, and the edges run up to the rounded corners only.
  {
    constexpr int X = 60, Y = 60, W = 80, H = 40, R = 6;
    clear();
    renderer.drawRoundedRect(X, Y, W, H, 1, R, true, true, false, false, true);
    check(!isInk(X, Y + H - 1) && !isInk(X + W - 1, Y + H - 1), "open corners stay open");
    check(!isInk(X + W / 2, Y + H - 1), "no bottom edge without a bottom corner");
    check(isInk(X + W / 2, Y) && isInk(X, Y + H / 2), "top and side edges drawn");
  }

  // The cover mask paints the page back over exactly the pixels the profile cuts, at all four
  // corners, and nothing inside.
  for (const int r : {6, 11}) {
    constexpr int X = 30, Y = 30, W = 236, H = 356;
    clear(0x00);  // an all-black "cover"
    renderer.maskRoundedRectOutsideCorners(X, Y, W, H, r);
    uint8_t cut[continuouscorner::MAX_ROWS];
    const int rows = continuouscorner::profile(r, std::min(W, H) / 2, cut);
    int wrong = 0;
    for (int i = 0; i < H; ++i)
      for (int j = 0; j < W; ++j)
        wrong += isInk(X + j, Y + i) != expectedInk(W, H, cut, rows, {true, true, true, true}, i, j) ? 1 : 0;
    check(wrong == 0, "mask clears the profile's corners, r", r, wrong);
  }

  // Clear paints nothing; a gray fill keeps its dither inside the same outline.
  clear();
  renderer.fillRoundedRect(20, 20, 100, 50, 8, Color::Clear);
  check(std::all_of(pixels.begin(), pixels.end(), [](uint8_t b) { return b == 0xff; }), "Clear draws nothing");
  clear();
  renderer.fillRoundedRect(20, 20, 100, 50, 8, Color::DarkGray);
  check(!isInk(20, 20) && !isInk(21, 20), "gray fill keeps the corner white");
  check(isInk(70, 20) || isInk(71, 20), "gray fill dithers the edge row");

  if (failures) return 1;
  std::puts("PASS: fills, borders and cover masks follow the continuous corner profile");
  return 0;
}
