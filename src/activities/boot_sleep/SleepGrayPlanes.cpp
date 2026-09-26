#include "SleepGrayPlanes.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
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

const uint8_t SleepGrayPlanes::LEVELS[4][4] = {{0, 0, 0, 0}, {DARK[0], DARK[1], DARK[2], DARK[3]},
                                                {LIGHT[0], LIGHT[1], LIGHT[2], LIGHT[3]}, {0xFF, 0xFF, 0xFF, 0xFF}};

SleepGrayPlanes::~SleepGrayPlanes() = default;

void SleepGrayPlanes::clearPasses(GfxRenderer& renderer) {
  for (const uint8_t fill : {uint8_t{0x00}, uint8_t{0xFF}}) {
    LOG_DBG("SLP", "clear %s", fill == 0x00 ? "black" : "white");
    renderer.clearScreen(fill);
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  }
}

bool SleepGrayPlanes::settle(GfxRenderer& renderer) {
  if (!clear) return false;
  clear = false;
  clearPasses(renderer);
  return true;
}

bool SleepGrayPlanes::keep() {
  const uint8_t* fb = renderer.getFrameBuffer();
  const size_t size = renderer.getBufferSize();
  if (size > (static_cast<size_t>(CHUNKS) << CHUNK_BITS)) return false;
#ifdef SIMULATOR
  // The heap a running BLE radio leaves at sleep has no room for the kept frame; lets a test
  // walk that path.
  if (std::getenv("CROSSPOINT_SIM_SLEEP_NO_HEAP")) {
    LOG_ERR("SLP", "Sleep frame: no heap to keep it");
    return false;
  }
#endif
  for (size_t at = 0, c = 0; at < size; at += size_t{1} << CHUNK_BITS, c++) {
    const size_t n = std::min(size - at, size_t{1} << CHUNK_BITS);
    kept[c].reset(new (std::nothrow) uint8_t[n]);  // no zero fill, the copy below fills it
    if (!kept[c]) {
      for (auto& piece : kept) piece.reset();
      LOG_ERR("SLP", "Sleep frame: no heap to keep it");
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
  uint8_t* fb = renderer.getFrameBuffer();
  const size_t size = renderer.getBufferSize();
  const auto at = [&](const size_t i) -> uint8_t& {
    return kept[i >> CHUNK_BITS][i & ((size_t{1} << CHUNK_BITS) - 1)];
  };
  // Without a kept plane the framebuffer is shown as it is: the MSB plane is the image's own
  // B/W threshold. The folded frame goes into the kept pieces, where it outlives the passes.
  if (kept[0]) {
    const size_t rowBytes = renderer.getDisplayWidthBytes();
    for (size_t i = 0; i < size; i++) {
      const int y = static_cast<int>(i / rowBytes) & 3;
      const uint8_t k = at(i);
      // Absolute planes: LSB (kept) white on levels 1 and 3, MSB white on 2 and 3. Overlay: the
      // kept B/W frame, and an MSB mask white on both gray levels, which share a checkerboard.
      at(i) = overlay ? k | (fb[i] & (0xAA >> (y & 1))) : (fb[i] & (k | LIGHT[y])) | (k & DARK[y]);
    }
  }
  LOG_INF("SLP", "Timing frame-ready at=%lu", static_cast<unsigned long>(millis()));
  renderer.setRenderMode(GfxRenderer::BW);  // also cancels an absolute pass that was begun
  // No heap to hold the frame: it is shown without the passes rather than lost to them.
  if (clear && (kept[0] || keep())) clearPasses(renderer);
  clear = false;
  for (size_t from = 0, c = 0; kept[0] && from < size; from += size_t{1} << CHUNK_BITS, c++) {
    memcpy(fb + from, kept[c].get(), std::min(size - from, size_t{1} << CHUNK_BITS));
  }
  for (auto& piece : kept) piece.reset();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
