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

// A quarter circle of radius r sampled at pixel centres: the pixels of row d (from the edge row)
// whose centres lie outside it.
int circleCut(const int r, const int d) {
  int n = 0;
  for (int i = 0; i < r; ++i) {
    const double dx = r - i - 0.5, dy = r - d - 0.5;
    n += d < r && dx * dx + dy * dy > static_cast<double>(r) * r ? 1 : 0;
  }
  return n;
}

// The same construction worked out independently in double precision with the library's trig:
// the x where the continuous corner of radius r and smoothing s crosses the line y = row + 0.5,
// in the corner's frame (origin at the square corner).
double crossing(const double r, const double s, const double y) {
  const double deg = 3.14159265358979 / 180, p = (1 + s) * r, arc = 90 * (1 - s);
  const double section = std::sin(arc / 2 * deg) * r * std::sqrt(2.0);
  const double alpha = (90 - arc) / 2, beta = 45 * s;
  const double c = r * std::tan(alpha / 2 * deg) * std::cos(beta * deg), d = c * std::tan(beta * deg);
  const double b = (p - section - c - d) / 3, a = 2 * b, ex = section + d;
  const auto bx = [&](const double t) {
    const double u = 1 - t;
    return u * u * u * p + 3 * u * u * t * (p - a) + 3 * u * t * t * (p - a - b) + t * t * t * ex;
  };
  if (y >= p) return 0;
  if (y > d && y <= ex) return r - std::sqrt(r * r - (r - y) * (r - y));
  double lo = 0, hi = 1;
  for (int k = 0; k < 60; ++k) {
    const double t = (lo + hi) / 2;
    ((y <= d ? d * t * t * t < y : bx(t) > y) ? lo : hi) = t;
  }
  const double t = (lo + hi) / 2;
  return y <= d ? bx(t) : d * t * t * t;
}

constexpr int BIG_BUDGET = 1000;

}  // namespace

int main() {
  uint8_t cut[continuouscorner::MAX_ROWS];

  // 1. Shape. A pixel belongs to the corner when its centre does. With no smoothing the profile is
  // the quarter circle sampled at pixel centres, pixel for pixel, at every radius drawn.
  for (int r = 1; r <= continuouscorner::MAX_RADIUS; ++r) {
    const int rows = continuouscorner::profile(r, BIG_BUDGET, cut, 0.0f);
    for (int d = 0; d < r; ++d) {
      const int got = d < rows ? cut[d] : 0;
      if (got != circleCut(r, d)) {
        printf("FAIL: radius %d row %d cuts %d, the circle %d\n", r, d, got, circleCut(r, d));
        assert(false);
      }
    }
  }

  // With the corner smoothing (s = 0.6), at every radius: the rows cut are those whose centre lies
  // before p = 1.6 r, the table never cuts more on a lower row, it is symmetric about the diagonal
  // (row d loses what column d loses: the rows that lose more than d), and every row matches the
  // curve worked out in double precision, bar a row whose crossing sits within 1/100 px of a pixel
  // centre. Up to 12 px, where the curve's gentle ends stay under half a pixel, every row is within
  // one pixel of the plain circle: no shelf runs along the edge.
  for (int r = 1; r <= continuouscorner::MAX_RADIUS; ++r) {
    const int rows = continuouscorner::profile(r, BIG_BUDGET, cut);
    assert(rows <= static_cast<int>(std::ceil(1.6 * r - 0.5)));
    for (int d = 0; d + 1 < rows; ++d) assert(cut[d + 1] <= cut[d]);
    for (int d = 0; d < rows; ++d) {
      int deeper = 0;
      for (int e = 0; e < rows; ++e) deeper += cut[e] > d ? 1 : 0;
      if (deeper != cut[d]) {
        printf("FAIL: radius %d row %d cuts %d but %d rows cut deeper\n", r, d, cut[d], deeper);
        assert(false);
      }
      const double x = crossing(r, 0.6, d + 0.5) - 0.5;
      const int exact = x > 0 ? static_cast<int>(std::ceil(x)) : 0;
      const double tie = x - std::floor(x);
      if (cut[d] != exact && tie > 0.01 && tie < 0.99) {
        printf("FAIL: radius %d row %d cuts %d, the curve %d\n", r, d, cut[d], exact);
        assert(false);
      }
      if (r <= 12 && std::abs(cut[d] - circleCut(r, d)) > 1) {
        printf("FAIL: radius %d row %d cuts %d, far from the circle's %d\n", r, d, cut[d], circleCut(r, d));
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
    assert(continuouscorner::profile(8, 10, cut) <= 10);  // s = 10 / 8 - 1 = 0.25, p = 10
    uint8_t circle[continuouscorner::MAX_ROWS];
    const int rows = continuouscorner::profile(6, 6, cut);  // radius 8 in a 12 px box: a plain r = 6 circle
    assert(rows == continuouscorner::profile(6, BIG_BUDGET, circle, 0.0f));
    for (int d = 0; d < rows; ++d) assert(cut[d] == circle[d]);
    assert(continuouscorner::profile(8, 0, cut) == 0);
    assert(continuouscorner::profile(0, 50, cut) == 0);
  }

  // Selections with inverted text: the corner never reaches the text. A FreeInkUI row (7 px corner)
  // sets its text 8 px in and well over 8 px down; the marks drawn by hand keep fitted(r, pad), and
  // with the smoothing their corner stays within `pad` at the column where the text starts.
  const auto depthAt = [&](const int column) {
    int deep = 0;
    for (int d = 0; d < continuouscorner::MAX_ROWS; ++d) deep += cut[d] > column ? 1 : 0;
    return deep;
  };
  for (auto& n : cut) n = 0;
  continuouscorner::profile(7, 26, cut);
  assert(depthAt(8) <= 8);
  for (int r = 1; r <= tenorradius::LEAF_MAX; ++r)
    for (int pad = 1; pad <= 12; ++pad) {
      const int rows = continuouscorner::profile(tenorradius::fitted(r, pad), BIG_BUDGET, cut);
      for (int d = rows; d < continuouscorner::MAX_ROWS; ++d) cut[d] = 0;
      assert(depthAt(pad) <= pad);
    }

  // 2. Sizes.
  // Leaf blocks: three twentieths of the short side, 3 to 14 px. The anchor is the Quotes book
  // list's selected row, 66 px tall at the smallest UI size (measured on the simulator): 10 px,
  // which drawn at pixel centres matches the corner of that row as it was chosen.
  static_assert(tenorradius::leaf(66) == 10);
  static_assert(tenorradius::leaf(52) == 8);   // a FreeInkUI list row at the smallest size
  static_assert(tenorradius::leaf(38) == 6);   // the Quotes top row
  static_assert(tenorradius::leaf(28) == 4);   // the Quotes number box, its narrow side
  static_assert(tenorradius::leaf(47) == 7);   // a keyboard key, its narrow side
  static_assert(tenorradius::leaf(12) == 3 && tenorradius::leaf(1) == 3);
  static_assert(tenorradius::leaf(100) == 14 && tenorradius::leaf(400) == 14);
  // Containers keep their children's corners concentric: outer = inner + inset.
  static_assert(tenorradius::container(7, 20) == 27);
  // Nested shapes the other way: inner = outer - inset, never under 2.
  static_assert(tenorradius::nest(27, 20) == 7);
  static_assert(tenorradius::nest(8, 7) == 2 && tenorradius::nest(3, 10) == 2);
  static_assert(tenorradius::nest(tenorradius::container(7, 20), 20) == 7);
  // Covers grow with their width, a sixteenth, never under 6 px.
  static_assert(tenorradius::cover(96) == 6);
  static_assert(tenorradius::cover(236) == 15);
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

  puts("PASS: continuous corners sampled at pixel centres match the circle unsmoothed and the curve "
       "smoothed, and the leaf, container, nest and cover sizes hold their anchors");
}
