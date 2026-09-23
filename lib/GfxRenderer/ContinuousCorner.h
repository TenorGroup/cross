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
// corner. Returns the number of rows that lose a pixel; rows past it are not cut.
//
// A pixel belongs to the shape when its centre does (it is at least half covered): row d is cut
// where the curve crosses the line y = d + 0.5, and loses the pixels whose centres lie before
// that crossing. Sampling at the centres keeps the raster as even as the curve: the gentle ends
// of a continuous corner, less than half a pixel off the edge, cut nothing, instead of turning
// into a long one-pixel shelf along the edge. The shape is symmetric about the corner's diagonal
// and so is the sampling, so column d loses exactly what row d loses, and callers use one table
// for all four corners.
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
  detail::sinCos(quarter * (1 - s), sinHalfArc, cosHalfArc);    // half the arc, 45 (1 - s) degrees
  detail::sinCos(quarter * s / 2, sinHalfAlpha, cosHalfAlpha);  // half of alpha = 45 s / 2
  detail::sinCos(quarter * s, sinBeta, cosBeta);                // beta = 45 s
  const float p = (1.0f + s) * r;
  const float arcSection = sinHalfArc * r * 1.41421356f;
  const float c = r * sinHalfAlpha / cosHalfAlpha * cosBeta;
  const float d = c * sinBeta / cosBeta;
  const float b = (p - arcSection - c - d) / 3;
  const float a = 2 * b;
  const float ex = arcSection + d;  // = p - a - b - c

  // The first Bezier at parameter t: x falls from p to ex and y (= d t^3, every control point but
  // the last sits on the edge) rises from 0 to d, both monotonic, so halving on t inverts either.
  const auto bezierX = [&](const float t) {
    const float u = 1 - t;
    return u * u * u * p + 3 * u * u * t * (p - a) + 3 * u * t * t * (p - a - b) + t * t * t * ex;
  };
  const auto solve = [&](const bool alongX, const float target) {
    float lo = 0, hi = 1;
    for (int k = 0; k < 24; ++k) {
      const float t = (lo + hi) / 2;
      const bool before = alongX ? bezierX(t) > target : d * t * t * t < target;
      (before ? lo : hi) = t;
    }
    return (lo + hi) / 2;
  };

  int rows = static_cast<int>(p + 0.5f);  // rows whose centre lies before p: ceil(p - 0.5)
  if (rows < p - 0.5f) ++rows;
  if (rows > MAX_ROWS) rows = MAX_ROWS;
  int used = 0;
  for (int row = 0; row < rows; ++row) {
    const float y = row + 0.5f;
    int n;
    if (y > d && y <= ex) {
      // The arc, in whole numbers so a circle's own ties land exactly: pixel i's centre lies
      // outside it when (r - i - 0.5)^2 + (r - y)^2 > r^2, that is (2r - 2i - 1)^2 > N below.
      const int twiceOff = 2 * radius - 2 * row - 1;  // 2 (r - y)
      const int n4 = 4 * radius * radius - twiceOff * twiceOff;
      n = 0;
      while (n < radius && (2 * radius - 2 * n - 1) * (2 * radius - 2 * n - 1) > n4) ++n;
    } else {
      // A Bezier: on the first, the x where its y reaches the row; on the second (the mirror of
      // the first), the first one's y where its x reaches the row.
      float x;
      if (y <= d) {
        x = bezierX(solve(false, y));
      } else {
        const float t = solve(true, y);
        x = d * t * t * t;
      }
      // Pixels whose centres (i + 0.5) lie before the crossing.
      const float before = x - 0.5f;
      n = static_cast<int>(before);
      if (n < before) ++n;
      if (n < 0) n = 0;
    }
    cut[row] = static_cast<uint8_t>(n);
    if (n > 0) used = row + 1;
  }
  return used;
}

}  // namespace continuouscorner
