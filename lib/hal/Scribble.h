#pragma once

#include <cstdint>

// Scribbled gestures on a touch screen: a row struck out (one stroke out along it and back, maybe
// once more, without lifting), a ring drawn round a row, a tap, a swipe, or none of these. Pure: logical screen pixels and milliseconds in, a result out, no hardware.
// Whole numbers only, so the host tests and the device agree to the bit and no maths library
// is pulled in.
//
// The caller feeds one sample a pass (Scribbler::step): whether a contact is down and where.
// A stroke is the samples from touch-down to lift, at most MAX_POINTS of them in a fixed array,
// and it is decided on lift.
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
constexpr int STRIKE_MIN_W_PX = 96;   // strike: box at least this wide (11 mm) ...
constexpr int STRIKE_MAX_H_PX = 40;   // ... and at most this tall
constexpr int STRIKE_TURN_PCT = 20;   // strike: x coming back this % of the width is a turn ...
constexpr int STRIKE_MAX_PASSES = 5;  // ... 2 to this many passes ...
constexpr int STRIKE_PASS_PCT = 60;   // ... the two longest at least this % of the box width
constexpr int CORNER_EPS_PX = 10;     // corner finder tolerance, at least this many px ...
constexpr int CORNER_EPS_PCT = 10;    // ... or this % of the box diagonal
constexpr int CORNER_MIN_DEG = 30;    // a bend under this between two pieces is no corner
constexpr int CLOSE_PCT = 22;         // ring: start-to-end gap at most this % of the path length
constexpr int RING_MIN_PX = 30;       // ring: box at least this wide and tall (a row is 64 px)
constexpr int RING_TURN_DEG = 250;    // ring: winds at least this far round the middle of its box ...
constexpr int RING_ONE_WAY_PCT = 75;  // ... and at least this % of its winding goes the one way

enum class Kind : uint8_t { None, Tap, Swipe, Strike, Circle, Unknown };

inline const char* kindName(Kind k) {
  switch (k) {
    case Kind::Tap:
      return "tap";
    case Kind::Swipe:
      return "swipe";
    case Kind::Strike:
      return "strike";
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
  int16_t x = 0;    // where it aims: the middle of a strike, a ring or an unknown shape,
  int16_t y = 0;    // the tap point, a swipe's start
  Pt from{}, to{};  // first and last sample of the gesture (a swipe's direction)
  uint8_t strokes = 0;  // 1 once decided
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

// Struck out: flat and wide, out along the row and back at least once without lifting. The passes are
// the runs of x one way, a turn counted once x has come back STRIKE_TURN_PCT of the width; the two
// longest must span nearly the whole width, so a flick that jerks back at its end is no strike.
inline bool isStrike(const Pt* p, const int n, const Box& box) {
  const int w = box.x1 - box.x0, h = box.y1 - box.y0;
  if (w < STRIKE_MIN_W_PX || h > STRIKE_MAX_H_PX) return false;
  const int turn = w * STRIKE_TURN_PCT / 100;
  int start = p[0].x, far = p[0].x, dir = 0, passes = 0, longest = 0, second = 0;
  const auto pass = [&](const int len) {
    ++passes;
    if (len > longest) {
      second = longest;
      longest = len;
    } else if (len > second) {
      second = len;
    }
  };
  for (int i = 1; i < n; ++i) {
    const int x = p[i].x;
    if (dir == 0) {
      if (iabs(x - start) >= turn) dir = x > start ? 1 : -1;
      far = x;
    } else if ((x - far) * dir > 0) {
      far = x;
    } else if ((far - x) * dir >= turn) {
      pass(iabs(far - start));
      start = far;
      far = x;
      dir = -dir;
    }
  }
  pass(iabs(far - start));
  return passes <= STRIKE_MAX_PASSES && second * 100 >= STRIKE_PASS_PCT * w;
}

}  // namespace detail

// One stroke on its own.
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
  if (isStrike(p, n, r.box)) {
    r.kind = Kind::Strike;
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
      } else {
        touching = true;
        spoiled = false;
        stroke.begin(x, y, now);
      }
      return {};
    }
    if (!touching) return {};
    touching = false;
    stroke.finish();
    ended = true;
    return spoiled ? Result{} : classifyStroke(stroke.p, stroke.n);
  }
  // A second finger joined the stroke: it is the light's, never a mark.
  void spoil() { spoiled = touching; }

  // The stroke that ended on the last step, or nullptr. Valid until the next touch-down.
  const Stroke* endedStroke() const { return ended && !spoiled ? &stroke : nullptr; }

 private:
  Stroke stroke;
  bool touching = false;
  bool ended = false;
  bool spoiled = false;
};

}  // namespace scribble
