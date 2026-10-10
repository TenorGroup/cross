#include "TenorInk.h"

#include <Print.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "BitmapHelpers.h"
#include "GrayThumb.h"

void writeBmpHeader2bit(Print& out, int width, int height);

bool tenorink::writeCover(const uint8_t* gray, int sw, int sh, Print& out, int w, int h, bool four) {
  if (!gray || sw <= 0 || sh <= 0 || w <= 0 || h <= 0 || w > 800 || h > 800) return false;
  const int stride = four ? (w * 2 + 31) / 32 * 4 : (w + 31) / 32 * 4;
  auto row = makeUniqueNoThrow<uint8_t[]>(stride);
  auto bounds = makeUniqueNoThrow<int[]>(2 * w);
  AtkinsonDitherer dither(w, true, !four);
  if (!row || !bounds || !dither.isValid()) return false;
  const double step = std::min(double(sw) / w, double(sh) / h);
  const bool fullWidth = int64_t(sw) * h <= int64_t(sh) * w;
  const double left = fullWidth ? 0 : float((sw - w * step) / 2);
  const double top = fullWidth ? float((sh - h * step) / 2) : 0;
  const double right = fullWidth ? sw : float((sw + w * step) / 2);
  const double bottom = fullWidth ? float((sh + h * step) / 2) : sh;
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
    span(left + advance, stepX, sw, bounds[2*x], bounds[2*x+1]);
  }
  if (four) writeBmpHeader2bit(out, w, h);
  else writeBmpHeader1bit(out, w, h);
  for (int y = 0; y < h; ++y) {
    int y0, y1;
    const volatile double advance = (y + 0.5) * stepY;
    span(top + advance, stepY, sh, y0, y1);
    std::memset(row.get(), 0, stride);
    for (int x = 0; x < w; ++x) {
      const int x0 = bounds[2*x], x1 = bounds[2*x+1], nx = x1 - x0, ny = y1 - y0;
      const uint32_t cx = ((1u << 22) + nx / 2) / nx, cy = ((1u << 22) + ny / 2) / ny;
      uint32_t sum = 0;
      for (int sy = y0; sy < y1; ++sy) {
        uint32_t horizontal = 0;
        for (int sx = x0; sx < x1; ++sx) horizontal += gray[sy * sw + sx];
        sum += (uint64_t(horizontal) * cx + (1u << 21)) >> 22;
      }
      const auto level = dither.processPixel((uint64_t(sum) * cy + (1u << 21)) >> 22, x);
      if (four) row[x / 4] |= level << (6 - (x % 4) * 2);
      else row[x / 8] |= (level == 3) << (7 - x % 8);
    }
    dither.nextRow();
    if (out.write(row.get(), stride) != static_cast<size_t>(stride)) return false;
    if ((y & 7) == 7) vTaskDelay(1);
  }
  return true;
}
