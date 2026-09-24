#include "SleepGrayPlanes.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {
// 4x4 Bayer thresholds {0 8 2 10, 12 4 14 6, 3 11 1 9, 15 7 13 5}, one mask per row: a set bit
// turns the pixel white. Dark gray (level 1) is white below 4 (4 of 16), light gray (level 2)
// below 12 (12 of 16). These two cuts give an even dot grid; 6 and 11 read as stripes on the
// glass. Bit 7 is a byte's first pixel; the 4-pixel row repeats twice.
constexpr uint8_t DARK[4] = {0xAA, 0x00, 0xAA, 0x00};
constexpr uint8_t LIGHT[4] = {0xFF, 0x55, 0xFF, 0x55};
}  // namespace

SleepGrayPlanes::~SleepGrayPlanes() = default;

bool SleepGrayPlanes::keep() {
  const uint8_t* fb = renderer.getFrameBuffer();
  const size_t size = renderer.getBufferSize();
  if (size > (static_cast<size_t>(CHUNKS) << CHUNK_BITS)) return false;
  for (size_t at = 0, c = 0; at < size; at += size_t{1} << CHUNK_BITS, c++) {
    const size_t n = std::min(size - at, size_t{1} << CHUNK_BITS);
    kept[c].reset(new (std::nothrow) uint8_t[n]);  // no zero fill, the copy below fills it
    if (!kept[c]) {
      for (auto& piece : kept) piece.reset();
      LOG_ERR("SLP", "Sleep dither: no heap, plain B/W");
      return false;
    }
    memcpy(kept[c].get(), fb + at, n);
  }
  return true;
}

bool SleepGrayPlanes::base() {
  if (!fold) {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
    return true;
  }
  overlay = true;
  if (keep()) return true;
  // No room to keep the B/W frame: show it as it is.
  show();
  return false;
}

void SleepGrayPlanes::lsb() {
  if (!fold)
    renderer.copyGrayscaleLsbBuffers();
  else if (!overlay)
    keep();
}

void SleepGrayPlanes::show() {
  if (!fold) {
    renderer.copyGrayscaleMsbBuffers();
    renderer.displayGrayBuffer();
    return;
  }
  if (shown) return;
  shown = true;
  // Without a kept plane the framebuffer is shown as it is: the MSB plane is the image's own
  // B/W threshold.
  if (kept[0]) {
    uint8_t* fb = renderer.getFrameBuffer();
    const size_t size = renderer.getBufferSize();
    const size_t rowBytes = renderer.getDisplayWidthBytes();
    for (size_t i = 0; i < size; i++) {
      const int y = static_cast<int>(i / rowBytes) & 3;
      const uint8_t k = kept[i >> CHUNK_BITS][i & ((size_t{1} << CHUNK_BITS) - 1)];
      // Absolute planes: LSB (kept) white on levels 1 and 3, MSB white on 2 and 3. Overlay: the
      // kept B/W frame, and an MSB mask white on both gray levels, which share a checkerboard.
      fb[i] = overlay ? k | (fb[i] & (0xAA >> (y & 1))) : (fb[i] & (k | LIGHT[y])) | (k & DARK[y]);
    }
    for (auto& piece : kept) piece.reset();
  }
  renderer.setRenderMode(GfxRenderer::BW);  // also cancels an absolute pass that was begun
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
