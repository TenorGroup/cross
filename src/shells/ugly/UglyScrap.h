#pragma once
// The scribbles on the empty part of a quotation page of tenor/ugly, as decisions only: how many, which, how big and where, so
// that none of them touches the words of the page. Integers only and no hardware, so a host test runs it as it is. The same
// quote on the same page always scribbles the same (the seed is the quote and the page), so a page drawn again, or come back
// to, is the same pixels. UglyScrap.cpp draws what this places.
#include <cstdint>

#include "UglyDoodleData.h"

class GfxRenderer;

namespace ugly::scrap {

struct Rect {
  int x0, y0, x1, y1;  // x1 and y1 are exclusive
};
inline bool meets(const Rect& a, const Rect& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
inline Rect grown(const Rect& r, const int by) { return {r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by}; }

// What a scribble is: a doodle of the quotation set (the ku among them, see KU), one of the sleep doodles drawn small, a word of
// scrawl struck out with a line or a cross, a patch rubbed out with the pen, a word of scrawl in a pen circle, or a worm, a long
// wavy line of the pen.
enum class Kind : uint8_t { Doodle, Sleep, Strike, Rub, Circled, Worm };

struct Item {
  Kind kind = Kind::Doodle;
  uint8_t pic = 0;        // Doodle: index in doodledata::PICTURES; Sleep: index in the sleep stream
  uint8_t pct = 100;      // Doodle and Sleep: size in percent of the picture's own
  bool mirror = false;    // Doodle and Sleep: turned left to right (before the turn below)
  bool cross = false;     // Strike: a cross over the word, else one line through it
  Rect box{0, 0, 0, 0};   // all the ink of the scribble
  int x = 0, y = 0;       // Doodle and Sleep: where the middle of the picture's ink lands, the picture turning about it
                          // Strike, Rub and Circled: the top left of the word the scribble is made on
                          // Worm: the left end of the worm, on its middle line
  int w = 0, h = 0;       // Strike, Rub, Circled: that word's size; Worm: its length and the height of its waves
  int16_t pivX = 0, pivY = 0;  // Doodle and Sleep: the middle of the picture's ink on its own canvas, scaled
  int16_t sinQ = 0, cosQ = 256;  // Doodle and Sleep: the turn of the picture, in 1/256
  int8_t tilt = 0;        // Worm: how far its right end stands below its left
  uint8_t wave = 3;       // Worm: steps of 1/32 turn of its wave for each 6 px
  uint8_t phase = 0;      // Worm: where in its wave it starts, in 1/32 turn
  uint32_t seed = 0;
};

inline constexpr int MAX_ITEMS = 18;
inline constexpr int CLEAR = 14;       // air kept between a scribble and any word of the page
inline constexpr int GAP = 10;         // air between two scribbles
inline constexpr int MIN_BAND = 70;    // under this much room below the last words a page gets no scribble
inline constexpr int MIN_KU_BAND = 112;  // from this much room a page has one ku, and at least 3 worms
inline constexpr int PER_ITEM = 17000;   // px2 of room under the words for each scribble that is not a ku or a worm
inline constexpr int WORM_STEP = 6;      // px between two points of a worm
inline constexpr int MAX_OTHERS = 10;

inline uint32_t next(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  uint32_t h = state;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}

// The seed of a page: the quote and the page of it.
inline uint32_t seedOf(const uint64_t quote, const int page) {
  uint64_t h = quote * 0x9e3779b97f4a7c15ull + static_cast<uint64_t>(page + 1) * 0xbf58476d1ce4e5b9ull;
  h ^= h >> 31;
  h *= 0x94d049bb133111ebull;
  h ^= h >> 29;
  return static_cast<uint32_t>(h ^ (h >> 32)) | 1u;
}

// Sleep doodles that read small: the dog on the books, the moon, the calendar and the alarm.
inline constexpr uint8_t SLEEP_PICKS[4] = {2, 3, 4, 7};

// A dot of the pen is 3 wide and a line may stray a pixel off its stroke: the box of a picture keeps this much round its ink,
// whatever the picture is scaled to or turned by (the turn rounds a pixel too).
inline constexpr int PEN = 3;
inline constexpr int REACH = PEN + 1;

struct Extent {
  int lo, hi;  // the ink of a picture along one axis once scaled, both ends inclusive, on its own canvas
};
inline Extent scaledExtent(const doodledata::Box& b, const bool alongX, const int pct) {
  const int lo = alongX ? b.x0 : b.y0, hi = alongX ? b.x1 : b.y1;
  return {(lo + doodledata::PAD) * pct / 100, (hi - doodledata::PAD - 1) * pct / 100};
}

// 1/256 sine and cosine of the turns a ku makes, in degrees: -14, -7, 0, 7, 14.
inline constexpr int8_t TURN_DEG[5] = {-14, -7, 0, 7, 14};
inline constexpr int16_t TURN_SIN[5] = {-62, -31, 0, 31, 62};
inline constexpr int16_t TURN_COS[5] = {248, 254, 256, 254, 248};

struct Point {
  int x, y;
};
// Where a point of a picture's own canvas lands on the page: scaled, turned left to right when `mirror`, turned by the item's
// angle about the middle of the ink, which stands at (x, y).
inline Point map(const Item& item, const int x, const int y) {
  const int sx = x * item.pct / 100 - item.pivX, sy = y * item.pct / 100 - item.pivY;
  const int dx = item.mirror ? -sx : sx;
  return {item.x + (dx * item.cosQ - sy * item.sinQ) / 256, item.y + (dx * item.sinQ + sy * item.cosQ) / 256};
}

// The box a picture takes and where the middle of its ink stands in it (offX, offY from the box's top left): the four corners of
// the ink, turned.
struct Frame {
  int w, h, offX, offY;
  int16_t pivX, pivY;
};
inline Frame pictureFrame(const doodledata::Box& b, const int pct, const bool mirror, const int sinQ, const int cosQ) {
  const Extent ex = scaledExtent(b, true, pct), ey = scaledExtent(b, false, pct);
  Frame f{};
  f.pivX = static_cast<int16_t>((ex.lo + ex.hi) / 2);
  f.pivY = static_cast<int16_t>((ey.lo + ey.hi) / 2);
  const int left = f.pivX - ex.lo, right = ex.hi - f.pivX, up = f.pivY - ey.lo, down = ey.hi - f.pivY;
  const int xs[2] = {mirror ? right : -left, mirror ? -left : right};  // the two sides, after the turn left to right
  const int ys[2] = {-up, down};
  int minX = 1 << 20, maxX = -(1 << 20), minY = 1 << 20, maxY = -(1 << 20);
  for (const int dx : xs)
    for (const int dy : ys) {
      const int rx = (dx * cosQ - dy * sinQ) / 256, ry = (dx * sinQ + dy * cosQ) / 256;
      minX = rx < minX ? rx : minX, maxX = rx > maxX ? rx : maxX;
      minY = ry < minY ? ry : minY, maxY = ry > maxY ? ry : maxY;
    }
  f.w = maxX - minX + 1 + 2 * REACH;
  f.h = maxY - minY + 1 + 2 * REACH;
  f.offX = REACH - minX;
  f.offY = REACH - minY;
  return f;
}

// A worm: a line that wanders, `h` high in its waves and `w` long, point i at (x + i * WORM_STEP, y + tilt * i * STEP / w + a
// sine of the phase). The sine of a 32nd of a turn, in 1/256.
inline constexpr int16_t SIN32[32] = {0,    50,   98,   142,  181,  213,  237,  251,  256,  251,  237,  213,  181,  142,  98,  50,
                                      0,    -50,  -98,  -142, -181, -213, -237, -251, -256, -251, -237, -213, -181, -142, -98, -50};
inline int wormPoints(const Item& item) { return item.w / WORM_STEP + 1; }
inline Point wormPoint(const Item& item, const int i) {
  const int turn = (item.phase + i * item.wave) & 31;
  return {item.x + i * WORM_STEP, item.y + item.tilt * i * WORM_STEP / (item.w > 0 ? item.w : 1) + item.h * SIN32[turn] / 256};
}

// Places the scribbles of a page inside `area`, none within CLEAR of any of the `avoid` rects (the words) or within GAP of
// another. The room is the band of `area` under the lowest of the words. A page with less than MIN_BAND of it gets none; from
// MIN_KU_BAND it gets exactly one ku and 3 to 6 worms, set before anything else so that they always find their place, then the
// other scribbles as the room allows (more room, more of them). Returns how many are in `out` (cap at most). The same input
// always gives the same items.
inline int place(const Rect& area, const Rect* avoid, const int nAvoid, const uint32_t seed, Item* out, const int cap) {
  int lowest = area.y0;
  for (int i = 0; i < nAvoid; ++i) lowest = avoid[i].y1 > lowest ? avoid[i].y1 : lowest;
  const int bandTop = lowest + CLEAR;
  const int band = area.y1 - bandTop;
  const int width = area.x1 - area.x0;
  if (band < MIN_BAND || width < 120) return 0;
  uint32_t state = seed;
  int count = 0;

  // Looks for a free spot for a box of bw x bh, anywhere in the area for the first `anywhere` tries, then under the words.
  const auto find = [&](const int bw, const int bh, const int tries, const int anywhere, Rect& box) {
    if (bw > width || bh > area.y1 - area.y0) return false;
    for (int attempt = 0; attempt < tries; ++attempt) {
      const int top = attempt < anywhere ? area.y0 : bandTop;
      const int rows = area.y1 - top - bh;
      if (rows < 0) continue;
      const int x0 = area.x0 + static_cast<int>(next(state) % static_cast<uint32_t>(width - bw + 1));
      const int y0 = top + static_cast<int>(next(state) % static_cast<uint32_t>(rows + 1));
      const Rect candidate{x0, y0, x0 + bw, y0 + bh};
      bool free = true;
      for (int i = 0; free && i < nAvoid; ++i) free = !meets(candidate, grown(avoid[i], CLEAR));
      for (int i = 0; free && i < count; ++i) free = !meets(candidate, grown(out[i].box, GAP / 2));
      if (free) {
        box = candidate;
        return true;
      }
    }
    return false;
  };
  // A picture of the set or of the sleep screen, sized and turned as asked.
  const auto putPicture = [&](Item item, const doodledata::Box& tb, const int turn, const int tries, const int anywhere) {
    const Frame f = pictureFrame(tb, item.pct, item.mirror, TURN_SIN[turn], TURN_COS[turn]);
    Rect box;
    if (count >= cap || !find(f.w, f.h, tries, anywhere, box)) return false;
    item.box = box;
    item.x = box.x0 + f.offX;
    item.y = box.y0 + f.offY;
    item.pivX = f.pivX, item.pivY = f.pivY;
    item.sinQ = TURN_SIN[turn], item.cosQ = TURN_COS[turn];
    out[count++] = item;
    return true;
  };

  if (band >= MIN_KU_BAND) {
    // The ku: of 80, 66, 56 or 48 percent, the biggest of them that the room allows from a random one of the first three, turned a little, either way
    // round. Under the words the room is all free, and the smallest one turned the furthest still stands in MIN_KU_BAND, so it is
    // always found.
    Item ku;
    ku.kind = Kind::Doodle;
    ku.pic = static_cast<uint8_t>(doodledata::KU);
    ku.seed = next(state);
    ku.mirror = next(state) % 2;
    const int turn = static_cast<int>(next(state) % 5);
    static constexpr uint8_t SIZES[4] = {80, 66, 56, 48};
    for (int k = static_cast<int>(next(state) % 3); k < 4; ++k) {
      ku.pct = SIZES[k];
      const Frame f = pictureFrame(doodledata::BOX[doodledata::KU], ku.pct, ku.mirror, TURN_SIN[turn], TURN_COS[turn]);
      if (f.h <= band && putPicture(ku, doodledata::BOX[doodledata::KU], turn, 60, 0)) break;
    }
  }
  // The worms.
  const int wanted = 3 + static_cast<int>(next(state) % 4);
  for (int worm = 0; worm < wanted && count < cap; ++worm) {
    Item w;
    w.kind = Kind::Worm;
    w.seed = next(state);
    w.h = 4 + static_cast<int>(next(state) % 8);
    w.tilt = static_cast<int8_t>(static_cast<int>(next(state) % 37) - 18);
    w.wave = static_cast<uint8_t>(2 + next(state) % 3);
    w.phase = static_cast<uint8_t>(next(state) % 32);
    const int longest = 70 + static_cast<int>(next(state) % 130), h0 = w.h, tilt0 = w.tilt;
    for (int k = 0; k < 6; ++k) {  // a worm that finds no room is tried again shorter, flatter, with smaller waves
      w.w = longest - k * (longest - 48) / 5;
      w.w -= w.w % WORM_STEP;
      w.h = h0 > 3 + k ? h0 - k : 3;
      w.tilt = static_cast<int8_t>(k < 3 ? tilt0 : tilt0 / 3);
      const int below = w.tilt > 0 ? w.tilt : 0, above = w.tilt < 0 ? -w.tilt : 0;
      const int bw = w.w + 2 * REACH + 1, bh = 2 * w.h + above + below + 1 + 2 * REACH;
      Rect box;
      if (!find(bw, bh, 40, 12, box)) continue;
      w.box = box;
      w.x = box.x0 + REACH;
      w.y = box.y0 + REACH + w.h + above;
      out[count++] = w;
      break;
    }
  }
  // The others, as many as the room under the words asks for.
  int want = 1 + static_cast<int>(static_cast<int64_t>(band) * width / PER_ITEM);
  want = want > MAX_OTHERS ? MAX_OTHERS : want;
  for (int n = 0, placed = 0; n < want * 3 && placed < want && count < cap; ++n) {
    Item item;
    item.seed = next(state);
    const uint32_t roll = next(state) % 100;
    int bw = 0, bh = 0;
    if (roll < 48) {
      item.kind = Kind::Doodle;
      const uint8_t other = static_cast<uint8_t>(next(state) % (doodledata::COUNT - 1));  // not the ku: it is set above
      item.pic = static_cast<uint8_t>(other >= doodledata::KU ? other + 1 : other);
      static constexpr uint8_t PCT[3] = {80, 100, 120};
      item.pct = PCT[next(state) % 3];
    } else if (roll < 58) {
      item.kind = Kind::Sleep;
      item.pic = SLEEP_PICKS[next(state) % 4];
      item.pct = static_cast<uint8_t>(24 + next(state) % 10);
    } else if (roll < 74) {
      item.kind = Kind::Strike;
      item.cross = next(state) % 3 == 0;
      item.w = 70 + static_cast<int>(next(state) % 120);
      item.h = 22;
      bw = item.w + 16, bh = item.h + 8;
    } else if (roll < 88) {
      item.kind = Kind::Rub;
      item.w = 70 + static_cast<int>(next(state) % 100);
      item.h = 22 + static_cast<int>(next(state) % 16);
      bw = item.w + 8, bh = item.h + 8;
    } else {
      item.kind = Kind::Circled;
      item.w = 60 + static_cast<int>(next(state) % 60);
      item.h = 18;
      bw = item.w + 2 * 38, bh = item.h + 2 * 23;
    }
    if (item.kind == Kind::Doodle || item.kind == Kind::Sleep) {
      item.mirror = next(state) % 2;
      const doodledata::Box& tb = item.kind == Kind::Doodle ? doodledata::BOX[item.pic] : doodledata::SLEEP_BOX[item.pic];
      if (putPicture(item, tb, 2, 30, 30)) ++placed;
      continue;
    }
    Rect box;
    if (bh > band || !find(bw, bh, 30, 30, box)) continue;
    item.box = box;
    if (item.kind == Kind::Circled) {
      item.x = box.x0 + 38, item.y = box.y0 + 23;
    } else if (item.kind == Kind::Strike) {
      item.x = box.x0 + 8, item.y = box.y0 + 4;
    } else {
      item.x = box.x0 + 3, item.y = box.y0 + 3;
    }
    out[count++] = item;
    ++placed;
  }
  return count;
}

// Draws the items (nothing when the heap will not hold the strokes: the page is then without scribbles).
void draw(const GfxRenderer& renderer, const Item* items, int count);

}  // namespace ugly::scrap
