#pragma once

// The corner formula of tenor/cross, one source for every rounded shape the firmware draws.
// A corner is worked out, never picked: its shape from one construction, its size from the size
// of the block it rounds.
//
// 1. Shape. Every rounded block GfxRenderer draws (fillRoundedRect, drawRoundedRect,
//    maskRoundedRectOutsideCorners) takes its corner from continuouscorner::profile
//    (lib/GfxRenderer/ContinuousCorner.h): a continuous-curvature corner, a circular arc joined to
//    each edge by a cubic Bezier, reaching p = (1 + s) r along each edge (the Figma
//    corner-smoothing construction, s = 0.6). A pixel belongs to the block when its centre does.
//    A block too small for p gives up smoothing first, then radius. A border is the outer block
//    less the inner block set in by its width, the inner corner concentric at radius r - width,
//    each row run on to touch the next so a thin line never breaks.
//
// 2. Size.
//    leaf(h)                 blocks that hold content directly (a selection, a list row, a key,
//                            a button, a number box): three twentieths of the short side h, 3 to 14 px.
//                            Anchor: the Quotes book list's selected row, 66 px tall at the
//                            smallest UI size (measured on the simulator), gives 10 px: drawn at
//                            pixel centres, the same corner that row had when it was chosen as
//                            the look to follow.
//    container(childR, inset) blocks that hold rounded blocks (a sheet, a popup): the child's
//                            radius plus the inset between them, so both corners share a centre
//                            (the concentric rule).
//    nest(outerR, inset)     a block set inside a rounded one: outer radius less the inset, never
//                            under 2.
//    cover(w)                book covers: a sixteenth of the width, never under 6 px.
//    fitted(r, pad)          a mark that hugs text keeps at most the radius whose corner stays off
//                            text inset `pad` px.
namespace tenorradius {

inline constexpr int LEAF_MIN = 3;
inline constexpr int LEAF_MAX = 14;
inline constexpr int NEST_MIN = 2;

constexpr int leaf(const int shortSide) {
  const int r = (3 * shortSide + 10) / 20;  // 3 h / 20, rounded to the nearest pixel
  return r < LEAF_MIN ? LEAF_MIN : r > LEAF_MAX ? LEAF_MAX : r;
}

constexpr int container(const int childRadius, const int inset) { return childRadius + inset; }

constexpr int nest(const int outerRadius, const int inset) {
  return outerRadius - inset > NEST_MIN ? outerRadius - inset : NEST_MIN;
}

// Covers come in very different sizes: a fixed corner that suits a small sleep-screen cover
// vanishes on the Recent card. A sixteenth of the width reads as rounded at every cover size.
constexpr int cover(const int width) { return (width + 8) / 16 > 6 ? (width + 8) / 16 : 6; }

// At the column where the content starts, a corner of radius r still covers
// r - sqrt(2 r pad - pad^2) px from the top; that stays within `pad` while r <= (2 + sqrt 2) pad,
// taken here as pad * 17 / 5. A mark hugging a word (2 px) gets at most 6; a list row (8 px) keeps
// its leaf radius.
constexpr int fitted(const int radius, const int pad) {
  const int cap = pad * 17 / 5;
  return radius < cap ? radius : cap;
}

}  // namespace tenorradius
