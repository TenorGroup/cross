// The tenor/cross corner formula (components/themes/TenorRadius.h) and the corner shape every
// rounded block is drawn with (lib/GfxRenderer/ContinuousCorner.h). Checked here because a corner
// that is the wrong size, or bites into the text it marks, only shows on the panel.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "ContinuousCorner.h"
#include "components/themes/TenorRadius.h"

namespace {

// How far down from the top edge a circular corner of radius `r` still covers the column `x` px
// in from the side edge: the white the corner leaves over content that starts at that column.
double cornerDepthAt(const int r, const int x) {
  if (x >= r) return 0;
  const double dx = r - x;
  return r - std::sqrt(static_cast<double>(r) * r - dx * dx);
}

// The quarter circle GfxRenderer drew before the continuous corner, row by row, exactly as its
// fillArc/drawArc walked it: the centre on the pixel `radius` in from both edges, a pixel kept
// while x^2 + dy^2 <= radius^2. Returns the pixels row d (from the edge row) lost.
int oldArcCut(const int radius, const int d) {
  const int dy = radius - d;
  if (dy < 0) return 0;
  int x = radius;
  while (x > 0 && x * x + dy * dy > radius * radius) --x;
  return radius - x;
}

constexpr int BIG_BUDGET = 1000;

}  // namespace

int main() {
  uint8_t cut[continuouscorner::MAX_ROWS];

  // 1. Shape. With no smoothing the profile is the old quarter circle, pixel for pixel, at every
  // radius the renderer draws.
  for (int r = 1; r <= continuouscorner::MAX_RADIUS; ++r) {
    const int rows = continuouscorner::profile(r, BIG_BUDGET, cut, 0.0f);
    if (rows != r) {
      printf("FAIL: radius %d with no smoothing spans %d rows, the circle %d\n", r, rows, r);
      assert(false);
    }
    for (int d = 0; d < rows; ++d) {
      if (cut[d] != oldArcCut(r, d)) {
        printf("FAIL: radius %d row %d cuts %d, the old circle %d\n", r, d, cut[d], oldArcCut(r, d));
        assert(false);
      }
    }
  }

  // With the corner smoothing (s = 0.6) the corner starts bending 1.6 r from the square corner,
  // never cuts more on a lower row than on the row above, and is symmetric about its diagonal:
  // the pixels row d loses equal the rows that lose more than d pixels.
  for (int r = 1; r <= continuouscorner::MAX_RADIUS; ++r) {
    // The rows that lose a pixel are those less than p = 1.6 r from the corner, bar the last few
    // where the curve runs within 1/1000 px of the edge and the pixel on it stays. The curve leaves
    // the edge as the cube of the distance, d (x / 3a)^3 with d ~ 0.11 r and a ~ 0.56 r, so that
    // tail is about 0.35 r^(2/3) rows long.
    const int rows = continuouscorner::profile(r, BIG_BUDGET, cut);
    const int span = static_cast<int>(std::ceil(1.6 * r - 1e-3));
    const int tail = 1 + static_cast<int>(0.4 * std::cbrt(static_cast<double>(r) * r));
    if (rows > span || rows < span - tail) {
      printf("FAIL: radius %d smoothed spans %d rows, p reaches %d\n", r, rows, span);
      assert(false);
    }
    assert(rows > r);  // the smoothing shows: the corner lets go of the edge before the circle would
    for (int d = 0; d + 1 < rows; ++d) assert(cut[d + 1] <= cut[d]);
    for (int d = 0; d < rows; ++d) {
      int deeper = 0;
      for (int e = 0; e < rows; ++e) deeper += cut[e] > d ? 1 : 0;
      if (std::abs(deeper - cut[d]) > 0) {
        printf("FAIL: radius %d row %d cuts %d but %d rows cut deeper\n", r, d, cut[d], deeper);
        assert(false);
      }
    }
  }

  // The corner's own sine and cosine (a series, to keep the library's range reduction out of flash)
  // agree with the library over the only angles it is given, 0 to 45 degrees.
  for (int k = 0; k <= 450; ++k) {
    const float x = 0.78539816f * k / 450;
    float sine, cosine;
    continuouscorner::detail::sinCos(x, sine, cosine);
    assert(std::fabs(sine - std::sin(x)) < 2e-7f && std::fabs(cosine - std::cos(x)) < 2e-7f);
  }

  // A small shape gives up smoothing first, then radius: a corner never runs past half the short
  // side, so the two corners of one edge never meet.
  {
    const int rows = continuouscorner::profile(8, 10, cut);  // s = 10 / 8 - 1 = 0.25
    assert(rows == 10);
    uint8_t circle[continuouscorner::MAX_ROWS];
    assert(continuouscorner::profile(6, 6, cut) == 6);  // radius 8 in a 12 px box: a plain r = 6 circle
    continuouscorner::profile(6, BIG_BUDGET, circle, 0.0f);
    for (int d = 0; d < 6; ++d) assert(cut[d] == circle[d]);
    assert(continuouscorner::profile(8, 0, cut) == 0);
    assert(continuouscorner::profile(0, 50, cut) == 0);
  }

  // 2. Sizes.
  // Leaf blocks: an eighth of the short side, 3 to 12 px. The anchor is the Quotes book list's
  // selected row, 66 px tall at the smallest UI size (measured on the simulator): 8 px.
  static_assert(tenorradius::leaf(66) == 8);
  static_assert(tenorradius::leaf(52) == 7);   // a FreeInkUI list row at the smallest size
  static_assert(tenorradius::leaf(38) == 5);   // the Quotes top row
  static_assert(tenorradius::leaf(28) == 4);   // the Quotes number box, its narrow side
  static_assert(tenorradius::leaf(47) == 6);   // a keyboard key, its narrow side
  static_assert(tenorradius::leaf(12) == 3 && tenorradius::leaf(1) == 3);
  static_assert(tenorradius::leaf(100) == 12 && tenorradius::leaf(400) == 12);
  // Containers keep their children's corners concentric: outer = inner + inset.
  static_assert(tenorradius::container(7, 20) == 27);
  // Nested shapes the other way: inner = outer - inset, never under 2.
  static_assert(tenorradius::nest(27, 20) == 7);
  static_assert(tenorradius::nest(8, 7) == 2 && tenorradius::nest(3, 10) == 2);
  static_assert(tenorradius::nest(tenorradius::container(7, 20), 20) == 7);
  // Covers grow with their width, about a twentieth, never under 6 px.
  static_assert(tenorradius::cover(96) == 6);
  static_assert(tenorradius::cover(236) == 11);
  static_assert(tenorradius::cover(40) == 6);

  // fitted() never grows a radius, and what it returns keeps the corner off content inset `pad`:
  // at the column where the content starts, the corner reaches no deeper than the same inset.
  for (int r = 0; r <= 16; ++r) {
    for (int pad = 0; pad <= 12; ++pad) {
      const int fit = tenorradius::fitted(r, pad);
      assert(fit <= r && fit >= 0);
      assert(cornerDepthAt(fit, pad) <= pad + 1e-9);
      // And it is the largest such radius, so marks stay as round as their padding allows.
      if (fit < r) assert(cornerDepthAt(fit + 1, pad) > pad);
    }
  }
  static_assert(tenorradius::fitted(8, 8) == 8);
  static_assert(tenorradius::fitted(8, 2) == 6);

  puts("PASS: continuous corners match the old circle unsmoothed, span 1.6 r smoothed, and the "
       "leaf, container, nest and cover sizes hold their anchors");
}
