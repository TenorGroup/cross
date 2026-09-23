#pragma once

#include <stdint.h>

// Continuous-curvature corners, the Figma corner-smoothing construction, in place of a quarter
// circle.
//
// A quarter circle joins a straight edge with a jump in curvature, and the eye reads that jump
// as a hard kink. A continuous corner starts bending earlier and more gently: a circular arc in
// the middle, joined to each edge by a cubic Bezier whose curvature rises from zero. The
// construction is the published corner-smoothing one: with radius r and smoothing s the corner
// runs p = (1 + s) r along each edge, the middle arc sweeps 90 (1 - s) degrees; s = 0.6 is the
// smoothing used here. s = 0 gives back the plain quarter circle.
//
// A corner that does not fit (p larger than the budget, half the short side) first gives up
// smoothing, then radius, so small marks stay rounded instead of turning into circles.
namespace continuouscorner {

inline constexpr float SMOOTHING = 0.6f;
// Largest radius drawn. 40 px keeps the profile table on the stack small (p <= 64 rows).
inline constexpr int MAX_RADIUS = 40;
inline constexpr int MAX_ROWS = 64;


namespace detail {
// sin and cos of x in [0, pi/4] from their Taylor series, good to float precision there. Every
// angle the corner needs lies in that range, and the library functions' range reduction alone
// would cost kilobytes of flash on a chip without a floating-point unit.
inline void sinCos(const float x, float& sine, float& cosine) {
  const float x2 = x * x;
  sine = x * (1 + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
  cosine = 1 + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320 - x2 * (1.0f / 3628800)))));
}
}  // namespace detail

// Fills cut[d] with how many pixels of row d (counted from the corner's edge row) lie outside
// the corner, for a corner of `radius` inside a shape whose short side allows `budget` px per
// corner. Returns the number of rows that lose a pixel; rows past it are not cut. The profile is
// symmetric about the corner's diagonal, so column d loses what row d loses, and callers use one
// table for all four corners.
//
// Pixels are sampled where the renderer has always put them: the edge row and column lie on the
// corner's straight lines, the way the old quarter circle stood with its centre on the pixel
// `radius` in from both edges. With s = 0 the table is that circle, pixel for pixel, so a radius
// keeps the size it had; with s = 0.6 the gentle ends of the curve show as the edge row and
// column letting go of the corner earlier, over the extra 0.6 r.
inline int profile(int radius, int budget, uint8_t* cut, float smoothing = SMOOTHING) {
  if (radius > MAX_RADIUS) radius = MAX_RADIUS;
  if (radius > budget) radius = budget;
  if (radius <= 0) return 0;
  float s = static_cast<float>(budget) / radius - 1.0f;
  if (s > smoothing) s = smoothing;
  if (s < 0) s = 0;

  // The published construction, in the corner's own frame: origin at the square corner, x along
  // the top edge, y along the side edge. The first Bezier runs from (p, 0) through (p - a, 0) and
  // (p - a - b, 0) to (ex, d); the arc of radius r about (r, r) runs from there to (d, ex), sweeping
  // 90 (1 - s) degrees; the second Bezier is the first mirrored about the diagonal.
  const float r = static_cast<float>(radius);
  const float quarter = 0.78539816f;  // 45 degrees
  float sinHalfArc, cosHalfArc, sinHalfAlpha, cosHalfAlpha, sinBeta, cosBeta;
  detail::sinCos(quarter * (1 - s), sinHalfArc, cosHalfArc);  // half the arc, 45 (1 - s) degrees
  detail::sinCos(quarter * s / 2, sinHalfAlpha, cosHalfAlpha);  // half of alpha = 45 s / 2
  detail::sinCos(quarter * s, sinBeta, cosBeta);               // beta = 45 s
  const float p = (1.0f + s) * r;
  const float arcSection = sinHalfArc * r * 1.41421356f;
  const float c = r * sinHalfAlpha / cosHalfAlpha * cosBeta;
  const float d = c * sinBeta / cosBeta;
  const float b = (p - arcSection - c - d) / 3;
  const float a = 2 * b;
  const float ex = arcSection + d;  // = p - a - b - c

  int rows = static_cast<int>(p);
  if (rows < p) ++rows;  // ceil(p): every row less than p from the corner
  if (rows > MAX_ROWS) rows = MAX_ROWS;
  // First each row's crossing, measured along the row. That measure is sound where the curve is
  // steep; on the flat rows next to the edge, the edge row itself lies on the curve's tangent and a
  // crossing along it says nothing. So the table is then made symmetric from the steep side: pixel
  // (i, d) above the diagonal goes exactly when its mirror (d, i) does.
  uint8_t along[MAX_ROWS];
  for (int row = 0; row < rows; ++row) {
    if (row <= d) {
      // First Bezier, all of it above the diagonal: only the mirrors count here.
      along[row] = static_cast<uint8_t>(rows);
    } else if (row <= ex) {
      // The arc, in whole numbers: ceil(r - sqrt(n)) is r - floor(sqrt(n)), a square root or not,
      // which keeps a pixel on the curve the way the old circle did, so an unsmoothed corner
      // lands on its pixels exactly.
      const int n = radius * radius - (radius - row) * (radius - row);
      int root = 0;
      while ((root + 1) * (root + 1) <= n) ++root;
      along[row] = static_cast<uint8_t>(radius - root);
    } else {
      // Second Bezier, the mirror of the first: find t where the first one's x equals the row, and
      // take the first one's y there, d t^3. x only falls as t grows, so halving finds it.
      float lo = 0, hi = 1;
      for (int k = 0; k < 24; ++k) {
        const float t = (lo + hi) / 2, u = 1 - t;
        const float x = u * u * u * p + 3 * u * u * t * (p - a) + 3 * u * t * t * (p - a - b) + t * t * t * ex;
        (x > row ? lo : hi) = t;
      }
      const float t = (lo + hi) / 2;
      // A pixel whose centre sits within float rounding of the curve stays.
      const float x = d * t * t * t - 1e-3f;
      int inset = static_cast<int>(x);
      if (inset < x) ++inset;
      along[row] = static_cast<uint8_t>(inset > 0 ? inset : 0);
    }
  }
  int used = 0;
  for (int row = 0; row < rows; ++row) {
    int n = along[row] < row + 1 ? along[row] : row + 1;  // pixels left of the diagonal
    for (int i = row + 1; i < rows; ++i) n += along[i] > row ? 1 : 0;  // mirrors of the steep side
    cut[row] = static_cast<uint8_t>(n);
    if (n > 0) used = row + 1;
  }
  return used;
}

}  // namespace continuouscorner
