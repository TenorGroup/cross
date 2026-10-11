#include "TenorInk.h"

#include <HeapFloor.h>

#include <Print.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "BitmapHelpers.h"
#include "GrayThumb.h"

void writeBmpHeader2bit(Print& out, int width, int height);

tenorink::CoverStream::CoverStream(int sw, int sh, Print& output, int w, int h, bool levels)
    : out(output), sourceWidth(sw), width(w), height(h),
      stride(0), four(levels) {
  if (sw <= 0 || sh <= 0 || !frameFits(w, h)) { failed = true; return; }
  stride = four ? (w * 2 + 31) / 32 * 4 : (w + 31) / 32 * 4;
  packed = makeUniqueNoThrow<uint8_t[]>(stride);
  bounds = heapfloor::array<int[]>(3 * w, "ink-bounds");
  rows = heapfloor::array<int[]>(2 * h, "ink-rows");
  sums = makeUniqueNoThrow<uint32_t[]>(w);
  dither = makeUniqueNoThrow<AtkinsonDitherer>(w, true, !four);
  if (!valid()) { failed = true; return; }
  const double scale = std::max(double(w) / sw, double(h) / sh);
  const double cropWidth = w / scale, cropHeight = h / scale;
  const double left = float(std::max(0.0, (sw - cropWidth) / 2));
  const double top = float(std::max(0.0, (sh - cropHeight) / 2));
  const double right = float(std::min(double(sw), (sw + cropWidth) / 2));
  const double bottom = float(std::min(double(sh), (sh + cropHeight) / 2));
  const double stepX = (right - left) / w, stepY = (bottom - top) / h;
  // BOX includes source pixel centers inside each cell; upscale selects the nearest center.
  const auto span = [](double center, double stepSize, int size, int& lo, int& hi) {
    const double radius = std::max(1.0, stepSize) / 2;
    lo = std::clamp(int(center - radius + 0.5), 0, size - 1);
    hi = std::clamp(int(center + radius + 0.5), lo + 1, size);
    const double reciprocal = 1.0 / std::max(1.0, stepSize);
    if (hi - lo > 1 && (lo - center + 0.5) * reciprocal <= -0.5) ++lo;
    if (hi - lo > 1 && (hi - 1 - center + 0.5) * reciprocal > 0.5) --hi;
  };
  for (int x = 0; x < w; ++x) {
    const volatile double advance = (x + 0.5) * stepX;
    span(left + advance, stepX, sw, bounds[3*x], bounds[3*x+1]);
    const int n = bounds[3*x+1] - bounds[3*x];
    bounds[3*x+2] = ((1u << 22) + n / 2) / n;
  }
  for (int y = 0; y < h; ++y) {
    const volatile double advance = (y + 0.5) * stepY;
    span(top + advance, stepY, sh, rows[2*y], rows[2*y+1]);
  }
  if (four) writeBmpHeader2bit(out, w, h);
  else writeBmpHeader1bit(out, w, h);
}

tenorink::CoverStream::~CoverStream() = default;

bool tenorink::CoverStream::valid() const {
  return !failed && packed && bounds && rows && sums && dither && dither->isValid();
}

bool tenorink::CoverStream::push(const uint8_t* sourceRow, int y) {
  if (!sourceRow || !valid()) return false;
  while (current < height && y >= rows[2*current]) {
    const int y1 = rows[2*current+1];
    if (y >= y1) { failed = true; return false; }
    for (int x = 0; x < width; ++x) {
      const int x0 = bounds[3*x], x1 = bounds[3*x+1];
      const uint32_t cx = bounds[3*x+2];
      uint32_t horizontal = 0;
      for (int sx = x0; sx < x1; ++sx) horizontal += sourceRow[sx];
      sums[x] += (uint64_t(horizontal) * cx + (1u << 21)) >> 22;
    }
    if (y != y1 - 1) break;
    std::memset(packed.get(), 0, stride);
    const int ny = y1 - rows[2*current];
    const uint32_t cy = ((1u << 22) + ny / 2) / ny;
    for (int x = 0; x < width; ++x) {
      const auto level = dither->processPixel((uint64_t(sums[x]) * cy + (1u << 21)) >> 22, x);
      if (four) packed[x / 4] |= level << (6 - (x % 4) * 2);
      else packed[x / 8] |= (level == 3) << (7 - x % 8);
      sums[x] = 0;
    }
    dither->nextRow();
    if (out.write(packed.get(), stride) != static_cast<size_t>(stride)) { failed = true; return false; }
    if ((current & 7) == 7) vTaskDelay(1);
    ++current;
  }
  return true;
}

bool tenorink::writeCover(const uint8_t* gray, int sw, int sh, Print& out, int w, int h, bool four) {
  if (!gray) return false;
  CoverStream stream(sw, sh, out, w, h, four);
  if (!stream.valid()) return false;
  for (int y = 0; y < sh; ++y) if (!stream.push(gray + size_t(y) * sw, y)) return false;
  return stream.complete();
}
