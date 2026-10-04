// The cover thumbnail built inside the cover page's decode (GrayThumb). On the X3 the previous
// build wrote thumbnail rows to the card from inside that decode and gathered whole source rows
// in RAM: the decode went from 2.1 s to 4.1 s and the lowest free heap from 84 KB to 71 KB.
// These cases hold the decode side to its buffers and nothing else, and the result to the same
// pixels the cover-file decode gives.
#include <Arduino.h>
#include <GrayThumb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "BitmapHelpers.h"
#include "activities/boot_sleep/SleepQuoteLayout.h"
#include "components/HomeExcerptStyle.h"

EspClass ESP;

namespace {
bool tracking = false;
size_t trackedBytes = 0, trackedCount = 0;
void* take(size_t size) {
  if (tracking) {
    trackedBytes += size;
    ++trackedCount;
    ESP.taken += size;
  }
  return std::malloc(size ? size : 1);
}
}  // namespace

void* operator new(size_t size) {
  if (void* p = take(size)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t size) {
  if (void* p = take(size)) return p;
  throw std::bad_alloc();
}
void* operator new(size_t size, const std::nothrow_t&) noexcept { return take(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return take(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::printf("FAIL %s\n", what);
  }
}

class Output : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t write(uint8_t v) override { return write(&v, 1); }
  size_t write(const uint8_t* data, size_t n) override {
    bytes.insert(bytes.end(), data, data + n);
    return n;
  }
};

// A cover-like source: tones in both directions plus a sharp bar, so area sums and the dither
// both leave their mark.
uint8_t pixel(int x, int y) {
  if (y > 300 && y < 340 && x > 100 && x < 600) return 250;
  return static_cast<uint8_t>((x * 131 + y * 71 + (x * y) / 97) % 256);
}

// The decode side: JPEGDEC hands MCU rows as blocks left to right.
std::vector<uint8_t> block(16 * 128);
bool feed(GrayThumb& thumb, int w, int h, int blockRows, int blockCols) {
  for (int y = 0; y < h; y += blockRows) {
    const int rows = std::min(blockRows, h - y);
    for (int x = 0; x < w; x += blockCols) {
      const int cols = std::min(blockCols, w - x);
      for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) block[r * blockCols + c] = pixel(x + c, y + r);
      thumb.block(x, y, cols, rows, block.data(), blockCols);
    }
  }
  return thumb.finish();
}

// The cover-file path (JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize): whole source rows,
// area sums and counts per thumbnail column, a row flushed once the source passes its end.
std::vector<uint8_t> reference(int w, int h, int height) {
  // Fill the Recent card's 236 x 356 shape in whole pixels: the side that decides lands exactly.
  const int tw = height * 236 / 356;
  const bool byWidth = tw * h >= height * w;
  const int ow = byWidth ? tw : w * height / h, oh = byWidth ? h * tw / w : height;
  const uint32_t sx = (static_cast<uint32_t>(w) << 16) / ow, sy = (static_cast<uint32_t>(h) << 16) / oh;
  const int stride = (ow + 31) / 32 * 4;
  Output out;
  writeBmpHeader1bit(out, ow, oh);
  std::vector<uint32_t> acc(ow), cnt(ow);
  std::vector<uint8_t> row(stride);
  FloydSteinberg1BitDitherer dither(ow);
  int outY = 0;
  uint32_t next = sy;
  for (int y = 0; y < h; y++) {
    for (int ox = 0; ox < ow; ox++) {
      const int a = (static_cast<uint32_t>(ox) * sx) >> 16, b = (static_cast<uint32_t>(ox + 1) * sx) >> 16;
      for (int s = a; s < b && s < w; s++) acc[ox] += pixel(s, y), cnt[ox]++;
    }
    if ((static_cast<uint32_t>(y + 1) << 16) >= next && outY < oh) {
      std::fill(row.begin(), row.end(), 0);
      for (int i = 0; i < ow; i++) {
        const int ox = dither.at(i);
        row[ox / 8] |= dither.processPixel(cnt[ox] ? acc[ox] / cnt[ox] : 0, ox) << (7 - ox % 8);
      }
      dither.nextRow();
      out.write(row.data(), row.size());
      outY++;
      next = static_cast<uint32_t>(outY + 1) * sy;
      std::fill(acc.begin(), acc.end(), 0);
      std::fill(cnt.begin(), cnt.end(), 0);
    }
  }
  return out.bytes;
}
}  // namespace

// One shape for the card and every thumbnail: a thumbnail of the card's height is the card's width.
static_assert(thumbWidthFor(HOME_CARD_COVER_H) == HOME_CARD_COVER_W, "thumbnail shape differs from the card's cover");
static_assert(thumbWidthFor(sleepquote::COVER_H) == sleepquote::COVER_W,
              "the quote sleep tile is not the card's shape");

int main() {
  check(THUMB_SHAPE_W == HOME_CARD_COVER_W && THUMB_SHAPE_H == HOME_CARD_COVER_H,
        "thumbnail proportions use the Recent card dimensions");
  for (int height = 120; height <= HOME_CARD_COVER_H; ++height)
    check(thumbWidthFor(height) == height * HOME_CARD_COVER_W / HOME_CARD_COVER_H,
          "thumbnail width matches the card at every supported height");
  {
    // Whole pixels on the side that decides: float scales came out one short for many sizes.
    int w = 0, h = 0;
    coverScaleSize(780, 1227, 236, 356, true, &w, &h);
    check(w == 236 && h == 371, "780x1227 does not fill 236x356 as 236x371");
    coverScaleSize(100, 157, 236, 356, true, &w, &h);
    check(w == 236 && h == 370, "100x157 does not fill 236x356 as 236x370");
    coverScaleSize(780, 1227, 528, 792, false, &w, &h);
    check(w == 503 && h == 792, "780x1227 does not fit 528x792 as 503x792");
    coverScaleSize(1227, 780, 528, 792, false, &w, &h);
    check(w == 528 && h == 335, "1227x780 does not fit 528x792 as 528x335");
  }
  // X3: a 1600x2560 cover on its half grid (800x1280), 4:2:0 MCU rows of 8 at that grid.
  constexpr int W = 800, H = 1280, ROWS = 8;
  {
    GrayThumb thumb(HOME_CARD_COVER_H);
    ESP.freeHeap = 90000;
    tracking = true;
    trackedBytes = trackedCount = 0;
    const bool started = thumb.start(W, H, ROWS);
    const size_t startBytes = trackedBytes;
    trackedBytes = trackedCount = 0;
    const bool finished = started && feed(thumb, W, H, ROWS, 128);
    const size_t feedCount = trackedCount;
    tracking = false;
    check(started, "start with the X3 heap at the cover decode");
    // The X3 bar: the lowest free heap of a new book open stays above ~70 KB. The card's cover went from
    // 236 x 356 to 298 x 450 on 04/10: 13.706 B became 21.104 B. start() still keeps RENDER_MARGIN free.
    check(startBytes <= 21 * 1024 + 256, "the decode side takes more than 21 KB");
    check(startBytes == GrayThumb::bufferBytes(HOME_CARD_COVER_H, W, H, ROWS), "start takes what bufferBytes says");
    check(feedCount == 0, "feeding the decode allocates");
    check(finished, "every row of the thumbnail in");
    Output out;
    check(thumb.writeTo(out) && out.bytes == reference(W, H, HOME_CARD_COVER_H),
          "the thumbnail differs from the cover-file decode");
    Output small;
    ESP.taken = 0;  // written once the page is on the panel, with the decoder gone
    const bool scaled = thumb.writeScaled(226, small);
    // The card's shape at 226 high is 149 wide: 149 x 238.
    check(scaled && small.bytes.size() == 62 + static_cast<size_t>(20) * 238 && small.bytes[18] == 149 &&
              small.bytes[22] == static_cast<uint8_t>(-238),
          "the theme thumbnail is not 149x238 scaled from the card's");
    std::printf("decode side: %zu bytes, thumbnail %zu bytes\n", startBytes, out.bytes.size());
    // A 780x1227 cover decoded 1:1 for its page, 4:2:0 blocks of 16 rows: the X3 log of v1.0.13 had
    // 14.432 B for the thumbnail of the 0.6 rule, the 236 x 356 card 14.470 B. The 298 x 450 card takes
    // 22.588 B there, 8.1 KB more, which this bound holds it to.
    const size_t common = GrayThumb::bufferBytes(HOME_CARD_COVER_H, 780, 1227, 16);
    std::printf("780x1227 cover page: %zu bytes\n", common);
    check(common <= 22588 + 64, "the card's shape takes more heap than measured on a common cover");
  }
  {
    // The theme's thumbnail fed the same blocks as the card's: area averaged from the decoded gray,
    // the pixels its own decode gives. writeScaled() dithered the card's dithered bits a second time.
    GrayThumb big(HOME_CARD_COVER_H), small(226);
    big.alsoFeed(&small);
    ESP.freeHeap = 200000;
    ESP.taken = 0;
    tracking = true;
    const bool started = big.start(W, H, ROWS);
    tracking = false;
    check(started && feed(big, W, H, ROWS, 128) && small.ready(), "one feed does not give both thumbnails");
    Output card, theme;
    check(big.writeTo(card) && card.bytes == reference(W, H, HOME_CARD_COVER_H), "the card's thumbnail changed beside the theme's");
    check(small.writeTo(theme) && theme.bytes == reference(W, H, 226),
          "the theme's thumbnail is not area averaged from the decode");
  }
  {
    // Heap for the card's thumbnail alone (the X3 cover page with its render margin): the card's
    // comes through whole, the theme's stays out and falls back to writeScaled().
    GrayThumb big(HOME_CARD_COVER_H), small(226);
    big.alsoFeed(&small);
    ESP.freeHeap = static_cast<uint32_t>(GrayThumb::bufferBytes(HOME_CARD_COVER_H, W, H, ROWS) + GrayThumb::RENDER_MARGIN + 1024);
    ESP.taken = 0;
    tracking = true;
    const bool started = big.start(W, H, ROWS);
    tracking = false;
    check(started && feed(big, W, H, ROWS, 128), "the card's thumbnail gave way to the theme's");
    check(!small.ready(), "the theme's thumbnail started past the render margin");
    ESP.taken = 0;  // written once the page is on the panel, with the decoder gone
    Output card, theme;
    check(big.writeTo(card) && card.bytes == reference(W, H, HOME_CARD_COVER_H), "the card's thumbnail without the theme's");
    check(big.writeScaled(226, theme) && theme.bytes.size() == 62 + static_cast<size_t>(20) * 238,
          "the fallback theme thumbnail");
    ESP.freeHeap = 90000;  // the heap of the cases below
  }
  {
    // One block row of 16 on a grid start() was told had 8: the thumbnail is dropped, not smeared.
    GrayThumb thumb(HOME_CARD_COVER_H);
    check(thumb.start(W, H, 8) && !feed(thumb, W, H, 16, 128), "a block taller than declared was accepted");
  }
  {
    // Blocks out of raster order.
    GrayThumb thumb(HOME_CARD_COVER_H);
    std::vector<uint8_t> block(8 * W, 128);
    check(thumb.start(W, H, 8), "start");
    thumb.block(0, 400, W, 8, block.data(), W);
    thumb.block(0, 0, W, 8, block.data(), W);
    check(!thumb.finish(), "blocks out of order were accepted");
  }
  {
    // A decode that stops early leaves no half thumbnail.
    GrayThumb thumb(HOME_CARD_COVER_H);
    std::vector<uint8_t> block(8 * W, 128);
    check(thumb.start(W, H, 8), "start");
    for (int y = 0; y < H - 8; y += 8) thumb.block(0, y, W, 8, block.data(), W);
    check(!thumb.finish(), "a decode missing its last rows gave a thumbnail");
  }
  {
    // Too little heap, or a source smaller than the thumbnail: declined, and the book falls back
    // to the cover-file decode when the reader closes.
    GrayThumb low(HOME_CARD_COVER_H), small(HOME_CARD_COVER_H);
    ESP.freeHeap = 70000;
    check(!low.start(W, H, 8), "started with the page render's margin gone");
    ESP.freeHeap = 200000;
    check(!small.start(200, 300, 8), "scaled a small image up");
  }
  std::printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
