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

EspClass ESP;

namespace {
bool tracking = false;
size_t trackedBytes = 0, trackedCount = 0;
void* take(size_t size) {
  if (tracking) {
    trackedBytes += size;
    ++trackedCount;
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
  const float sw = static_cast<float>(static_cast<int>(height * 0.6)) / w, sh = static_cast<float>(height) / h;
  const float scale = sw > sh ? sw : sh;
  const int ow = static_cast<int>(w * scale), oh = static_cast<int>(h * scale);
  const uint32_t sx = (static_cast<uint32_t>(w) << 16) / ow, sy = (static_cast<uint32_t>(h) << 16) / oh;
  const int stride = (ow + 31) / 32 * 4;
  Output out;
  writeBmpHeader1bit(out, ow, oh);
  std::vector<uint32_t> acc(ow), cnt(ow);
  std::vector<uint8_t> row(stride);
  Atkinson1BitDitherer dither(ow);
  int outY = 0;
  uint32_t next = sy;
  for (int y = 0; y < h; y++) {
    for (int ox = 0; ox < ow; ox++) {
      const int a = (static_cast<uint32_t>(ox) * sx) >> 16, b = (static_cast<uint32_t>(ox + 1) * sx) >> 16;
      for (int s = a; s < b && s < w; s++) acc[ox] += pixel(s, y), cnt[ox]++;
    }
    if ((static_cast<uint32_t>(y + 1) << 16) >= next && outY < oh) {
      std::fill(row.begin(), row.end(), 0);
      for (int ox = 0; ox < ow; ox++) {
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

int main() {
  // X3: a 1600x2560 cover on its half grid (800x1280), 4:2:0 MCU rows of 8 at that grid.
  constexpr int W = 800, H = 1280, ROWS = 8;
  {
    GrayThumb thumb(356);
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
    // The X3 bar: the lowest free heap of a new book open stays above ~70 KB.
    check(startBytes <= 14 * 1024, "the decode side takes more than 14 KB");
    check(startBytes == GrayThumb::bufferBytes(356, W, H, ROWS), "start takes what bufferBytes says");
    check(feedCount == 0, "feeding the decode allocates");
    check(finished, "every row of the thumbnail in");
    Output out;
    check(thumb.writeTo(out) && out.bytes == reference(W, H, 356),
          "the thumbnail differs from the cover-file decode");
    Output small;
    const bool scaled = thumb.writeScaled(226, small);
    check(scaled && small.bytes.size() == 62 + static_cast<size_t>(20) * 226 &&
              (small.bytes[18] == 141 || small.bytes[18] == 140) && small.bytes[22] == static_cast<uint8_t>(-226),
          "the theme thumbnail is not 141x226 (one column either way) scaled from the card's");
    std::printf("decode side: %zu bytes, thumbnail %zu bytes\n", startBytes, out.bytes.size());
  }
  {
    // One block row of 16 on a grid start() was told had 8: the thumbnail is dropped, not smeared.
    GrayThumb thumb(356);
    check(thumb.start(W, H, 8) && !feed(thumb, W, H, 16, 128), "a block taller than declared was accepted");
  }
  {
    // Blocks out of raster order.
    GrayThumb thumb(356);
    std::vector<uint8_t> block(8 * W, 128);
    check(thumb.start(W, H, 8), "start");
    thumb.block(0, 400, W, 8, block.data(), W);
    thumb.block(0, 0, W, 8, block.data(), W);
    check(!thumb.finish(), "blocks out of order were accepted");
  }
  {
    // A decode that stops early leaves no half thumbnail.
    GrayThumb thumb(356);
    std::vector<uint8_t> block(8 * W, 128);
    check(thumb.start(W, H, 8), "start");
    for (int y = 0; y < H - 8; y += 8) thumb.block(0, y, W, 8, block.data(), W);
    check(!thumb.finish(), "a decode missing its last rows gave a thumbnail");
  }
  {
    // Too little heap, or a source smaller than the thumbnail: declined, and the book falls back
    // to the cover-file decode when the reader closes.
    GrayThumb low(356), small(356);
    ESP.freeHeap = 70000;
    check(!low.start(W, H, 8), "started with the page render's margin gone");
    ESP.freeHeap = 200000;
    check(!small.start(200, 300, 8), "scaled a small image up");
  }
  std::printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
