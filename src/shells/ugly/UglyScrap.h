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

// What a scribble is: a doodle of the quotation set, one of the sleep doodles drawn small, a word of scrawl struck out with a
// line or a cross, a patch rubbed out with the pen, or a word of scrawl in a pen circle.
enum class Kind : uint8_t { Doodle, Sleep, Strike, Rub, Circled };

struct Item {
  Kind kind = Kind::Doodle;
  uint8_t pic = 0;        // Doodle: index in doodledata::PICTURES; Sleep: index in the sleep stream
  uint8_t pct = 100;      // Doodle and Sleep: size in percent of the picture's own
  bool mirror = false;    // Doodle and Sleep: turned left to right
  bool cross = false;     // Strike: a cross over the word, else one line through it
  Rect box{0, 0, 0, 0};   // all the ink of the scribble
  int x = 0, y = 0;       // Doodle and Sleep: where the picture's own (0, 0) lands (mirrored: where its x counts back from)
                          // the others: the top left of the word the scribble is made on
  int w = 0, h = 0;       // the others: that word's size
  uint32_t seed = 0;
};

inline constexpr int MAX_ITEMS = 10;
inline constexpr int CLEAR = 14;      // air kept between a scribble and any word of the page
inline constexpr int GAP = 10;        // air between two scribbles
inline constexpr int MIN_BAND = 70;   // under this much room below the last words a page gets no scribble
inline constexpr int PER_ITEM = 17000;  // px2 of room under the words for each scribble

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

// Where a point of a picture's own canvas lands on the page.
inline int mapX(const Item& item, const int x) { return item.mirror ? item.x - x * item.pct / 100 : item.x + x * item.pct / 100; }
inline int mapY(const Item& item, const int y) { return item.y + y * item.pct / 100; }

// A dot of the pen is 3 wide and a line may stray a pixel off its stroke: the box of a picture keeps this much round its ink,
// whatever the picture is scaled to.
inline constexpr int PEN = 3;
struct Extent {
  int lo, hi;  // the ink of a picture along one axis once scaled, both ends inclusive, from its own origin
};
inline Extent scaledExtent(const doodledata::Box& b, const bool alongX, const int pct) {
  const int lo = alongX ? b.x0 : b.y0, hi = alongX ? b.x1 : b.y1;
  return {(lo + doodledata::PAD) * pct / 100, (hi - doodledata::PAD - 1) * pct / 100};
}

// Places up to MAX_ITEMS scribbles inside `area`, none within CLEAR of any of the `avoid` rects (the words) or within GAP of
// another. The room is the band of `area` under the lowest of the words; a page whose words come close to the foot gets none.
// Returns how many are in `out` (cap at most). The same input always gives the same items.
inline int place(const Rect& area, const Rect* avoid, const int nAvoid, const uint32_t seed, Item* out, const int cap) {
  int lowest = area.y0;
  for (int i = 0; i < nAvoid; ++i) lowest = avoid[i].y1 > lowest ? avoid[i].y1 : lowest;
  const int band = area.y1 - lowest - CLEAR;
  const int width = area.x1 - area.x0;
  if (band < MIN_BAND || width < 120) return 0;
  int want = 1 + static_cast<int>(static_cast<int64_t>(band) * width / PER_ITEM);
  want = want > MAX_ITEMS ? MAX_ITEMS : want;
  want = want > cap ? cap : want;
  uint32_t state = seed;
  int count = 0;
  bool wall = false;
  for (int n = 0; n < want * 3 && count < want; ++n) {
    Item item;
    item.seed = next(state);
    const uint32_t roll = next(state) % 100;
    int bw = 0, bh = 0;
    if (roll < 48) {
      item.kind = Kind::Doodle;
      const uint32_t pick = next(state) % 20;  // the wall drawing, once at most, on about 3 pages in 20 doodles
      const uint8_t other = static_cast<uint8_t>(next(state) % (doodledata::COUNT - 1));
      item.pic = pick < 3 && !wall ? static_cast<uint8_t>(doodledata::WALL)
                                   : static_cast<uint8_t>(other >= doodledata::WALL ? other + 1 : other);
      static constexpr uint8_t PCT[3] = {80, 100, 120};
      item.pct = item.pic == doodledata::WALL ? (next(state) % 2 ? 100 : 90) : PCT[next(state) % 3];
      const Extent ex = scaledExtent(doodledata::BOX[item.pic], true, item.pct), ey = scaledExtent(doodledata::BOX[item.pic], false, item.pct);
      bw = ex.hi - ex.lo + 1 + 2 * PEN, bh = ey.hi - ey.lo + 1 + 2 * PEN;
    } else if (roll < 58) {
      item.kind = Kind::Sleep;
      item.pic = SLEEP_PICKS[next(state) % 4];
      item.pct = static_cast<uint8_t>(24 + next(state) % 10);
      const Extent ex = scaledExtent(doodledata::SLEEP_BOX[item.pic], true, item.pct), ey = scaledExtent(doodledata::SLEEP_BOX[item.pic], false, item.pct);
      bw = ex.hi - ex.lo + 1 + 2 * PEN, bh = ey.hi - ey.lo + 1 + 2 * PEN;
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
    item.mirror = (item.kind == Kind::Doodle || item.kind == Kind::Sleep) && next(state) % 2;
    if (bw > width || bh > band) continue;
    for (int attempt = 0; attempt < 30; ++attempt) {
      const int x0 = area.x0 + static_cast<int>(next(state) % static_cast<uint32_t>(width - bw + 1));
      const int rows = area.y1 - area.y0 - bh;
      if (rows < 0) break;
      const int y0 = area.y0 + static_cast<int>(next(state) % static_cast<uint32_t>(rows + 1));
      const Rect box{x0, y0, x0 + bw, y0 + bh};
      bool free = true;
      for (int i = 0; free && i < nAvoid; ++i) free = !meets(box, grown(avoid[i], CLEAR));
      for (int i = 0; free && i < count; ++i) free = !meets(box, grown(out[i].box, GAP / 2)) ;
      if (!free) continue;
      item.box = box;
      if (item.kind == Kind::Doodle || item.kind == Kind::Sleep) {
        const doodledata::Box& tb = item.kind == Kind::Doodle ? doodledata::BOX[item.pic] : doodledata::SLEEP_BOX[item.pic];
        const Extent ex = scaledExtent(tb, true, item.pct), ey = scaledExtent(tb, false, item.pct);
        item.x = item.mirror ? x0 + PEN + ex.hi : x0 + PEN - ex.lo;
        item.y = y0 + PEN - ey.lo;
      } else if (item.kind == Kind::Circled) {
        item.x = x0 + 38, item.y = y0 + 23;
      } else if (item.kind == Kind::Strike) {
        item.x = x0 + 8, item.y = y0 + 4;
      } else {
        item.x = x0 + 3, item.y = y0 + 3;
      }
      wall = wall || (item.kind == Kind::Doodle && item.pic == doodledata::WALL);
      out[count++] = item;
      break;
    }
  }
  return count;
}

// Draws the items (nothing when the heap will not hold the strokes: the page is then without scribbles).
void draw(const GfxRenderer& renderer, const Item* items, int count);

}  // namespace ugly::scrap
