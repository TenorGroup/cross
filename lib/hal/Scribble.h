#pragma once

#include <cstdint>

// Scribbled gestures on a touch screen: an X drawn over a row (two strokes, or one stroke going
// down one arm, up the side and down the other), a ring drawn round a row, a tap, a swipe, or
// none of these. Pure: logical screen pixels and milliseconds in, a result out, no hardware.
// Whole numbers only, so the host tests and the device agree to the bit and no maths library
// is pulled in.
//
// The caller feeds one sample a pass (Scribbler::step): whether a contact is down and where.
// A stroke is the samples from touch-down to lift, at most MAX_POINTS of them in a fixed array.
// A straight slanted stroke may be the first half of an X, so it is held up to PAIR_MS for a
// second one; every other stroke is decided on lift.
//
// The knobs below are first guesses on generated strokes (test/scribble). Tune them on strokes
// drawn on the device (CMD:STROKE_LOG, test/scribble/log_to_samples.py).
namespace scribble {

constexpr int MAX_POINTS = 128;
constexpr int MIN_STEP_PX = 4;        // a sample nearer than this to the last one kept is dropped
constexpr int TAP_PX = 24;            // box diagonal at most this: a tap
constexpr int MIN_SIZE_PX = 40;       // anything smaller that is not a tap is Unknown
constexpr int TURN_STEP_PX = 14;      // path length is taken over steps this long
constexpr int LINE_DEV_PCT = 15;      // straight: no point further off the chord than this % of it ...
constexpr int LINE_PATH_PCT = 125;    // ... and a path at most this % of the chord
constexpr int AXIS_DEG = 12;          // a line this near an axis is a swipe, never half an X
constexpr uint32_t PAIR_MS = 700;     // wait for an X's second stroke this long after the first
constexpr int CROSS_MIN_DEG = 25;     // X arms cross at between this and 180 - this degrees
constexpr int CROSS_MID_PCT = 15;     // the crossing lies within [this, 100 - this] % of each arm
constexpr int ARM_RATIO_PCT = 40;     // shorter X arm at least this % of the longer
constexpr int ARM_PCT = 45;           // one-stroke X: both arms at least this % of the box diagonal
constexpr int OPEN_PCT = 30;          // one-stroke X: ends at least this % of the box diagonal apart
constexpr int CORNER_EPS_PX = 10;     // corner finder tolerance, at least this many px ...
constexpr int CORNER_EPS_PCT = 10;    // ... or this % of the box diagonal
constexpr int CORNER_MIN_DEG = 30;    // a bend under this between two pieces is no corner
constexpr int CLOSE_PCT = 22;         // ring: start-to-end gap at most this % of the path length
constexpr int RING_MIN_PX = 30;       // ring: box at least this wide and tall (a row is 64 px)
constexpr int RING_TURN_DEG = 250;    // ring: winds at least this far round the middle of its box ...
constexpr int RING_ONE_WAY_PCT = 75;  // ... and at least this % of its winding goes the one way

enum class Kind : uint8_t { None, Tap, Swipe, Cross, Circle, Unknown };

inline const char* kindName(Kind k) {
  switch (k) {
    case Kind::Tap:
      return "tap";
    case Kind::Swipe:
      return "swipe";
    case Kind::Cross:
      return "cross";
    case Kind::Circle:
      return "circle";
    case Kind::Unknown:
      return "unknown";
    default:
      return "none";
  }
}

struct Pt {
  int16_t x;
  int16_t y;
  uint16_t t;  // ms since the stroke began, held at 65535
};

struct Box {
  int16_t x0, y0, x1, y1;  // inclusive
};

struct Result {
  Kind kind = Kind::None;
  Box box{};        // every stroke of the gesture
  int16_t x = 0;    // where it aims: the crossing of an X, the middle of a ring or of an
  int16_t y = 0;    // unknown shape, the tap point, a swipe's start
  Pt from{}, to{};  // first and last sample of the gesture (a swipe's direction)
  uint8_t strokes = 0;
};

// One stroke's samples. Full, it drops every other sample and doubles its step, so a long stroke
// keeps its whole shape in the same array. finish() puts the last sample seen at the end.
struct Stroke {
  Pt p[MAX_POINTS];
  uint8_t n = 0;
  uint32_t t0 = 0;
  int step = MIN_STEP_PX;

  void begin(int x, int y, uint32_t now) {
    n = 0;
    t0 = now;
    step = MIN_STEP_PX;
    push(x, y, now);
    last = p[0];
  }
  void add(int x, int y, uint32_t now) {
    last = make(x, y, now);
    const int dx = x - p[n - 1].x, dy = y - p[n - 1].y;
    if (dx * dx + dy * dy < step * step) return;
    if (n == MAX_POINTS) halve();
    push(x, y, now);
  }
  void finish() {
    const Pt& end = p[n - 1];
    if (end.x == last.x && end.y == last.y) return;
    if (n == MAX_POINTS) halve();
    p[n++] = last;
  }

 private:
  Pt last{};
  Pt make(int x, int y, uint32_t now) const {
    const uint32_t dt = now - t0;
    return Pt{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<uint16_t>(dt > 65535 ? 65535 : dt)};
  }
  void push(int x, int y, uint32_t now) { p[n++] = make(x, y, now); }
  void halve() {
    for (int i = 1; i < MAX_POINTS / 2; ++i) p[i] = p[2 * i];
    p[MAX_POINTS / 2] = p[MAX_POINTS - 1];  // the last kept sample stays last
    n = MAX_POINTS / 2 + 1;
    step *= 2;
  }
};

struct Seg {
  int x0, y0, x1, y1;
};

namespace detail {

inline int iabs(int v) { return v < 0 ? -v : v; }

inline uint32_t isqrt(uint32_t v) {
  uint32_t r = 0;
  uint32_t bit = 1u << 30;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return r;
}

inline int dist(int dx, int dy) { return static_cast<int>(isqrt(static_cast<uint32_t>(dx * dx + dy * dy))); }
inline int dist(const Pt& a, const Pt& b) { return dist(b.x - a.x, b.y - a.y); }

// Heading of (dx, dy) in tenths of a degree, 0..3599, from +x toward +y. atan(z) taken as
// 45z + 15.64z(1 - z) degrees on the first octant, within a quarter degree.
inline int angle10(int dx, int dy) {
  const int ax = iabs(dx), ay = iabs(dy);
  if (ax == 0 && ay == 0) return 0;
  const int mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
  const int32_t z = static_cast<int32_t>(mn) * 1024 / mx;
  int a = static_cast<int>((450 * z + 156 * z * (1024 - z) / 1024 + 512) / 1024);
  if (ay > ax) a = 900 - a;
  if (dx < 0) a = 1800 - a;
  if (dy < 0) a = 3600 - a;
  return a % 3600;
}

inline int wrap10(int d) {
  while (d > 1800) d -= 3600;
  while (d < -1800) d += 3600;
  return d;
}

// sin of a whole-degree knob in thousandths, worked out at compile time.
constexpr int sinPermille(int deg) {
  const double r = deg * 3.14159265358979 / 180.0;
  return static_cast<int>(1000.0 * (r - r * r * r / 6 + r * r * r * r * r / 120 - r * r * r * r * r * r * r / 5040) +
                          0.5);
}

inline Box boxOf(const Pt* p, int n) {
  Box b{p[0].x, p[0].y, p[0].x, p[0].y};
  for (int i = 1; i < n; ++i) {
    if (p[i].x < b.x0) b.x0 = p[i].x;
    if (p[i].x > b.x1) b.x1 = p[i].x;
    if (p[i].y < b.y0) b.y0 = p[i].y;
    if (p[i].y > b.y1) b.y1 = p[i].y;
  }
  return b;
}

inline int diagOf(const Box& b) { return dist(b.x1 - b.x0, b.y1 - b.y0); }

inline Box unite(Box a, const Box& b) {
  if (b.x0 < a.x0) a.x0 = b.x0;
  if (b.y0 < a.y0) a.y0 = b.y0;
  if (b.x1 > a.x1) a.x1 = b.x1;
  if (b.y1 > a.y1) a.y1 = b.y1;
  return a;
}

// Samples at least TURN_STEP_PX apart along the stroke, first and last always in. Path length
// and winding come from these, so a shaking finger does not lengthen the path.
inline int anchors(const Pt* p, int n, uint8_t* out) {
  int m = 0;
  out[m++] = 0;
  for (int i = 1; i < n; ++i) {
    if (dist(p[out[m - 1]], p[i]) >= TURN_STEP_PX) out[m++] = static_cast<uint8_t>(i);
  }
  if (out[m - 1] != n - 1) out[m++] = static_cast<uint8_t>(n - 1);
  return m;
}

// Straight: every sample near the chord and the path hardly longer than it.
inline bool isLine(const Pt* p, int n, int pathLen) {
  const int dx = p[n - 1].x - p[0].x, dy = p[n - 1].y - p[0].y;
  const int64_t chord2 = static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy;
  if (chord2 == 0) return false;
  if (static_cast<int64_t>(pathLen) * 100 > static_cast<int64_t>(LINE_PATH_PCT) * dist(dx, dy)) return false;
  for (int i = 1; i < n - 1; ++i) {
    const int64_t c = static_cast<int64_t>(dx) * (p[i].y - p[0].y) - static_cast<int64_t>(dy) * (p[i].x - p[0].x);
    if (c * c * 100 * 100 > chord2 * chord2 * LINE_DEV_PCT * LINE_DEV_PCT) return false;
  }
  return true;
}

// Corners by Douglas-Peucker, then bends gentler than CORNER_MIN_DEG merged away.
inline int corners(const Pt* p, int n, int eps, uint8_t* out) {
  bool keep[MAX_POINTS] = {};
  keep[0] = keep[n - 1] = true;
  uint8_t stack[2 * MAX_POINTS];
  int top = 0;
  stack[top++] = 0;
  stack[top++] = static_cast<uint8_t>(n - 1);
  while (top > 0) {
    const int j = stack[--top], i = stack[--top];
    const int dx = p[j].x - p[i].x, dy = p[j].y - p[i].y;
    const int len = dist(dx, dy);
    int64_t worst = -1;
    int at = -1;
    for (int k = i + 1; k < j; ++k) {
      const int64_t d = len == 0 ? dist(p[i], p[k])
                                 : detail::iabs(static_cast<int>(static_cast<int64_t>(dx) * (p[k].y - p[i].y) -
                                                                 static_cast<int64_t>(dy) * (p[k].x - p[i].x))) /
                                       len;
      if (d > worst) {
        worst = d;
        at = k;
      }
    }
    if (at >= 0 && worst > eps) {
      keep[at] = true;
      stack[top++] = static_cast<uint8_t>(i);
      stack[top++] = static_cast<uint8_t>(at);
      stack[top++] = static_cast<uint8_t>(at);
      stack[top++] = static_cast<uint8_t>(j);
    }
  }
  int m = 0;
  for (int i = 0; i < n; ++i)
    if (keep[i]) out[m++] = static_cast<uint8_t>(i);
  for (int k = 1; k + 1 < m;) {
    const Pt &a = p[out[k - 1]], &b = p[out[k]], &c = p[out[k + 1]];
    const int bend = wrap10(angle10(c.x - b.x, c.y - b.y) - angle10(b.x - a.x, b.y - a.y));
    if (iabs(bend) < CORNER_MIN_DEG * 10) {
      for (int r = k; r + 1 < m; ++r) out[r] = out[r + 1];
      --m;
    } else {
      ++k;
    }
  }
  return m;
}

inline Seg segOf(const Pt& a, const Pt& b) { return Seg{a.x, a.y, b.x, b.y}; }
inline int lenOf(const Seg& s) { return dist(s.x1 - s.x0, s.y1 - s.y0); }

inline bool armsMatch(const Seg& a, const Seg& b) {
  const int la = lenOf(a), lb = lenOf(b);
  const int lo = la < lb ? la : lb, hi = la < lb ? lb : la;
  return lo * 100 >= ARM_RATIO_PCT * hi;
}

inline bool slanted(const Seg& s) {
  const int a = angle10(s.x1 - s.x0, s.y1 - s.y0) % 900;
  return a > AXIS_DEG * 10 && a < 900 - AXIS_DEG * 10;
}

}  // namespace detail

// Two arms make an X: they cross at CROSS_MIN_DEG or more, inside the middle of both.
// (x, y) gets the crossing.
inline bool crossAt(const Seg& a, const Seg& b, int& x, int& y) {
  const int64_t ux = a.x1 - a.x0, uy = a.y1 - a.y0, vx = b.x1 - b.x0, vy = b.y1 - b.y0;
  const int64_t wx = b.x0 - a.x0, wy = b.y0 - a.y0;
  int64_t d = ux * vy - uy * vx;
  int64_t tn = wx * vy - wy * vx;
  int64_t sn = wx * uy - wy * ux;
  if (d == 0) return false;
  constexpr int64_t s = detail::sinPermille(CROSS_MIN_DEG);
  if (d * d * 1000 * 1000 < s * s * (ux * ux + uy * uy) * (vx * vx + vy * vy)) return false;
  if (d < 0) {
    d = -d;
    tn = -tn;
    sn = -sn;
  }
  const auto mid = [d](int64_t t) { return t * 100 >= CROSS_MID_PCT * d && t * 100 <= (100 - CROSS_MID_PCT) * d; };
  if (!mid(tn) || !mid(sn)) return false;
  x = static_cast<int>(a.x0 + (ux * tn + d / 2) / d);
  y = static_cast<int>(a.y0 + (uy * tn + d / 2) / d);
  return true;
}

// One stroke on its own. A slanted line comes back as a Swipe; the Scribbler is the one that
// holds it for a second stroke.
inline Result classifyStroke(const Pt* p, int n) {
  using namespace detail;
  Result r;
  r.box = boxOf(p, n);
  r.from = p[0];
  r.to = p[n - 1];
  r.strokes = 1;
  r.x = static_cast<int16_t>((r.box.x0 + r.box.x1) / 2);
  r.y = static_cast<int16_t>((r.box.y0 + r.box.y1) / 2);
  const int diag = diagOf(r.box);
  if (diag <= TAP_PX) {
    r.kind = Kind::Tap;
    r.x = p[0].x;
    r.y = p[0].y;
    return r;
  }
  r.kind = Kind::Unknown;
  if (diag < MIN_SIZE_PX) return r;

  uint8_t idx[MAX_POINTS];
  const int m = anchors(p, n, idx);
  int path = 0;
  for (int k = 1; k < m; ++k) path += dist(p[idx[k - 1]], p[idx[k]]);

  if (isLine(p, n, path)) {
    r.kind = Kind::Swipe;
    r.x = p[0].x;
    r.y = p[0].y;
    return r;
  }

  const int gap = dist(p[0], p[n - 1]);
  const int thin = r.box.x1 - r.box.x0 < r.box.y1 - r.box.y0 ? r.box.x1 - r.box.x0 : r.box.y1 - r.box.y0;
  if (gap * 100 <= CLOSE_PCT * path && thin >= RING_MIN_PX) {
    // How far the stroke winds round the middle of its box. Seen from the middle a ring sweeps
    // one way all round, a figure 8 sweeps back over itself; a shaking finger hardly moves it.
    // A ring is hollow: a stroke through the middle (a flat X, an 8) is none, and seen from the
    // middle its winding there would be a half turn either way.
    int net = 0, all = 0, prev = -1;
    bool hollow = true;
    for (int k = 0; k < m && hollow; ++k) {
      const int dx = p[idx[k]].x - r.x, dy = p[idx[k]].y - r.y;
      hollow = dist(dx, dy) * 4 >= thin;
      const int a = angle10(dx, dy);
      if (prev >= 0) {
        const int turn = wrap10(a - prev);
        net += turn;
        all += iabs(turn);
      }
      prev = a;
    }
    if (hollow && iabs(net) >= RING_TURN_DEG * 10 && iabs(net) * 100 >= RING_ONE_WAY_PCT * all) {
      r.kind = Kind::Circle;
      return r;
    }
  }

  if (gap * 100 >= OPEN_PCT * diag) {
    const int eps = diag * CORNER_EPS_PCT / 100 > CORNER_EPS_PX ? diag * CORNER_EPS_PCT / 100 : CORNER_EPS_PX;
    const int c = corners(p, n, eps, idx);
    for (int i = 0; i + 1 < c; ++i) {
      const Seg a = segOf(p[idx[i]], p[idx[i + 1]]);
      if (lenOf(a) * 100 < ARM_PCT * diag) continue;
      for (int j = i + 2; j + 1 < c; ++j) {
        const Seg b = segOf(p[idx[j]], p[idx[j + 1]]);
        int x = 0, y = 0;
        if (lenOf(b) * 100 >= ARM_PCT * diag && armsMatch(a, b) && crossAt(a, b, x, y)) {
          r.kind = Kind::Cross;
          r.x = static_cast<int16_t>(x);
          r.y = static_cast<int16_t>(y);
          return r;
        }
      }
    }
  }
  return r;
}

class Scribbler {
 public:
  // One pass: `down` with the contact at (x, y), or lifted. Returns the gesture once decided.
  Result step(bool down, int x, int y, uint32_t now) {
    ended = false;
    if (down) {
      if (touching) {
        stroke.add(x, y, now);
        return {};
      }
      touching = true;
      stroke.begin(x, y, now);
      return expired(now);  // a second stroke too late for the first: the first stands alone
    }
    if (touching) {
      touching = false;
      stroke.finish();
      ended = true;
      return lifted(now);
    }
    return expired(now);
  }

  // The stroke that ended on the last step, or nullptr. Valid until the next touch-down.
  const Stroke* endedStroke() const { return ended ? &stroke : nullptr; }
  // A slanted line waiting for the second half of an X.
  bool pending() const { return waiting; }

 private:
  Stroke stroke;
  bool touching = false;
  bool ended = false;
  bool waiting = false;
  uint32_t liftedAt = 0;
  Result first;

  Result expired(uint32_t now) {
    if (!waiting || now - liftedAt < PAIR_MS) return {};
    waiting = false;
    return first;
  }

  Result lifted(uint32_t now) {
    Result r = classifyStroke(stroke.p, stroke.n);
    const Seg s = detail::segOf(r.from, r.to);
    if (waiting) {
      waiting = false;
      const Seg a = detail::segOf(first.from, first.to);
      Result pair = r;
      pair.box = detail::unite(first.box, r.box);
      pair.from = first.from;
      pair.strokes = 2;
      int x = 0, y = 0;
      if (r.kind == Kind::Swipe && detail::armsMatch(a, s) && crossAt(a, s, x, y)) {
        pair.kind = Kind::Cross;
        pair.x = static_cast<int16_t>(x);
        pair.y = static_cast<int16_t>(y);
      } else {
        pair.kind = Kind::Unknown;
        pair.x = static_cast<int16_t>((pair.box.x0 + pair.box.x1) / 2);
        pair.y = static_cast<int16_t>((pair.box.y0 + pair.box.y1) / 2);
      }
      return pair;
    }
    if (r.kind == Kind::Swipe && detail::slanted(s)) {
      waiting = true;
      liftedAt = now;
      first = r;
      return {};
    }
    return r;
  }
};

}  // namespace scribble
