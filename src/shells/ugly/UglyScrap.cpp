#include "UglyScrap.h"

#include <GfxRenderer.h>
#include <HeapFloor.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "UglyDoodleData.h"
#include "UglyInk.h"
#include "UglySleepData.h"
#include "UglySleepSet.h"
#include "UglyWobble.h"
#include "components/X3BrandCodec.h"

namespace ugly::scrap {
namespace {

// A stream inflated into fresh heap, or null (free it). Held only while the scribbles are drawn.
uint8_t* unpack(const uint8_t* packed, const size_t packedSize, const size_t raw) {
  auto* out = static_cast<uint8_t*>(heapfloor::take(raw, "scrap-art"));
  if (out && !decodeX3BrandPlane(packed, packedSize, out, raw)) std::free(std::exchange(out, nullptr));
  return out;
}

void picture(const GfxRenderer& r, const uint8_t* data, const size_t raw, const Item& item) {
  // a doodle drawn small keeps a thin pen; the ku is drawn thin once it is under 70 percent, like the sleep doodles
  const int width = item.kind == Kind::Sleep || (item.pic == doodledata::KU && item.pct < 70) ? 2 : 0;
  sleepset::drawPicture(data, raw, item.pic, 0, 0, [&](int x0, int y0, int x1, int y1, int seed, int w) {
    const Point a = map(item, x0, y0), b = map(item, x1, y1);
    line(r, a.x, a.y, b.x, b.y, static_cast<uint32_t>(seed), width ? std::min(width, w) : w);
  });
}

// A worm: its points one after another, a dot at its head end.
void worm(const GfxRenderer& r, const Item& item) {
  Point from = wormPoint(item, 0);
  for (int i = 1; i < wormPoints(item); ++i) {
    const Point to = wormPoint(item, i);
    penStroke(r, from.x, from.y, to.x, to.y, 2);
    from = to;
  }
  penStroke(r, from.x, from.y, from.x, from.y, 3);
}

// A line of scrawl that says nothing, over the word box (x, y, w, h): small loops and spikes, the pen lifted now and then.
void scrawl(const GfxRenderer& r, const Item& item) {
  const int mid = item.y + item.h / 2;
  int px = item.x, py = mid;
  for (int i = 1, cx = item.x + 4; cx <= item.x + item.w; ++i, cx += 4) {
    int ny = mid + (i % 2 ? -1 : 1) * (2 + logic::wobble(item.seed, i, 2) + 2);
    if (i % 7 == 3) ny = mid - item.h / 2;  // a tall letter
    if (i % 9 == 8) {                       // a space between two words: the pen lifts
      cx += 6;
      px = cx;
      py = mid;
      continue;
    }
    penStroke(r, px, py, cx, ny, 2);
    px = cx;
    py = ny;
  }
}

void rub(const GfxRenderer& r, const Item& item) {
  const int rows = std::max(2, item.h / 5);
  int px = item.x, py = item.y;
  for (int i = 1; i <= 2 * rows; ++i) {
    const int x = i % 2 ? item.x + item.w : item.x;
    const int y = item.y + item.h * i / (2 * rows) + logic::wobble(item.seed, i, 1);
    penStroke(r, px, py, x, y, 2);
    px = x;
    py = y;
  }
}

void drawOne(const GfxRenderer& r, const Item& item, const uint8_t* doodles, const uint8_t* sleep) {
  switch (item.kind) {
    case Kind::Doodle:
      if (doodles) picture(r, doodles, doodledata::PICTURES_RAW, item);
      return;
    case Kind::Sleep:
      if (sleep) picture(r, sleep, sleepdata::PICTURES_RAW, item);
      return;
    case Kind::Strike:
      scrawl(r, item);
      if (item.cross) {
        line(r, item.x - 4, item.y - 2, item.x + item.w + 4, item.y + item.h + 2, item.seed, 2);
        line(r, item.x - 4, item.y + item.h + 2, item.x + item.w + 4, item.y - 2, item.seed + 1, 2);
      } else {
        line(r, item.x - 6, item.y + item.h / 2 + 3, item.x + item.w + 6, item.y + item.h / 2 - 3, item.seed, 2);
      }
      return;
    case Kind::Rub:
      rub(r, item);
      return;
    case Kind::Worm:
      worm(r, item);
      return;
    case Kind::Circled:
      scrawl(r, item);
      circle(r, Circle::Word, {item.x, item.y, item.x + item.w, item.y + item.h}, 36, 21, item.seed, 2);
      return;
  }
}

}  // namespace

void draw(const GfxRenderer& r, const Item* items, const int count) {
  if (count <= 0 || r.isFontCacheScanning()) return;
  bool wantDoodle = false, wantSleep = false;
  for (int i = 0; i < count; ++i) {
    wantDoodle = wantDoodle || items[i].kind == Kind::Doodle;
    wantSleep = wantSleep || items[i].kind == Kind::Sleep;
  }
  uint8_t* doodles = wantDoodle ? unpack(doodledata::PICTURES, sizeof(doodledata::PICTURES), doodledata::PICTURES_RAW) : nullptr;
  uint8_t* sleep = wantSleep ? unpack(sleepdata::PICTURES, sizeof(sleepdata::PICTURES), sleepdata::PICTURES_RAW) : nullptr;
  for (int i = 0; i < count; ++i) drawOne(r, items[i], doodles, sleep);
  std::free(doodles);
  std::free(sleep);
}

}  // namespace ugly::scrap
