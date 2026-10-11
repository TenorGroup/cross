#include <HeapFloor.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#define LOG_ERR(...) ((void)0)
static int rounds = 0;
static bool refuse = false;
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  return refuse ? nullptr : ::operator new[](size);
}
static double countedFloor(float value) { ++rounds; return std::floor(value); }
template <typename T> std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]());
}
enum class BmpReaderError { Ok, ShortReadRow };
struct Bitmap {
  int w, h;
  bool top;
  mutable int row = 0;
  int failRow = -1;
  int getWidth() const { return w; }
  int getHeight() const { return h; }
  int getRowBytes() const { return ((w + 31) / 32) * 4; }
  bool isTopDown() const { return top; }
  bool black(int x, int y) const { return x % 37 == 0 || y % 43 == 0 || x == y || (x * 71 + y * 13) % 997 == 0; }
  BmpReaderError readNextRow(uint8_t* out, uint8_t*) const {
    if (row == failRow) return BmpReaderError::ShortReadRow;
    std::memset(out, 255, (w + 3) / 4);
    const int y = top ? row : h - 1 - row;
    for (int x = 0; x < w; ++x) if (black(x, y)) out[x / 4] &= ~(3 << (6 - x % 4 * 2));
    ++row;
    return BmpReaderError::Ok;
  }
};
struct GfxRenderer {
  int w, h;
  mutable std::vector<uint8_t> frame;
  GfxRenderer(int width, int height) : w(width), h(height), frame((w + 7) / 8 * h, 0xa5) {}
  int getScreenWidth() const { return w; }
  int getScreenHeight() const { return h; }
  void drawPixel(int x, int y, bool) const { frame[y * ((w + 7) / 8) + x / 8] &= ~(0x80 >> (x % 8)); }
  void preserveImagePolarity(int, int, int, int) const {}
  bool drawBitmap1Bit(const Bitmap&, int, int, int, int) const;
};
#include "paint.inc"
int main() {
  int cases = 0, failures = 0;
  for (const int width : {17, 528, 1024, 2048}) for (bool top : {false, true})
    for (int offset : {-13, 0, 39}) for (int limit : {0, 240, 511}) {
      Bitmap bmp{width, 1031, top};
      GfxRenderer actual(528, 792), expected(528, 792);
      float scale = 1.0f;
      if (limit > 0 && width > limit) scale = static_cast<float>(limit) / width;
      if (limit > 0 && bmp.h > limit) scale = std::min(scale, static_cast<float>(limit) / bmp.h);
      for (int sy = 0; sy < bmp.h; ++sy) for (int sx = 0; sx < width; ++sx) {
        const int dx = offset + static_cast<int>(std::floor(sx * scale));
        const int dy = offset + static_cast<int>(std::floor(sy * scale));
        if (dx >= 0 && dx < expected.w && dy >= 0 && dy < expected.h && bmp.black(sx, sy))
          expected.drawPixel(dx, dy, true);
      }
      rounds = 0;
      const bool ok = actual.drawBitmap1Bit(bmp, offset, offset, limit, limit);
      if (!ok || actual.frame != expected.frame) { ++failures; std::puts("FAIL framebuffer bytes"); }
      if (rounds > width + bmp.h + 2) { ++failures; std::printf("FAIL rounding budget: %d > %d\n", rounds, width + bmp.h + 2); }
      ++cases;
    }
  Bitmap shortRow{100, 100, true, 0, 1};
  GfxRenderer target(528, 792);
  if (target.drawBitmap1Bit(shortRow, 0, 0, 50, 50)) ++failures;
  Bitmap denied{2048, 1031, true};
  const auto before = target.frame;
  refuse = true;
  if (target.drawBitmap1Bit(denied, 0, 0, 240, 240) || denied.row != 0 || target.frame != before) ++failures;
  refuse = false;
  std::printf("%d raster cases, short read, OOM, %d failures\n", cases, failures);
  return failures ? 1 : 0;
}
