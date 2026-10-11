#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

#include "../Memory/HeapFloor.h"

class Print;
class AtkinsonDitherer;

#ifndef TENOR_INK_HOME_LEVELS
#define TENOR_INK_HOME_LEVELS 4
#endif

namespace tenorink {
// The single Home gray policy, including the emergency monochrome switch.
constexpr bool homeGray(bool touch, bool psram, bool recent, bool ugly, bool absoluteSupported,
                        int levels = TENOR_INK_HOME_LEVELS, bool x3 = false,
                        size_t free = 0, size_t largest = 0) {
  // X3: the heap is on the floor before the 7,920 B strip is asked for (heapfloor, reported numbers).
  return recent && !ugly && absoluteSupported && levels == 4 &&
         ((touch && psram) || (x3 && !heapfloor::crosses(free, largest)));
}

// The frames the kernel takes, and the most it holds for one (bounds, rows, sums, a packed row, the
// ditherer's 3 error rows): one rule for CoverStream and for a caller that asks the heap first.
constexpr bool frameFits(int w, int h) { return w > 0 && h > 0 && w <= 800 && h <= 800; }
constexpr size_t streamBytes(int w, int h, bool four) {
  return size_t(four ? (w * 2 + 31) / 32 * 4 : (w + 31) / 32 * 4) + size_t(w) * 3 * sizeof(int) +
         size_t(h) * 2 * sizeof(int) + size_t(w) * sizeof(uint32_t) + (size_t(w) + 4) * 3 * sizeof(int16_t) + 64;
}

// One crop/BOX/dither kernel for both source arrays and decoded scanlines.
class CoverStream {
 public:
  CoverStream(int sw, int sh, Print& out, int w, int h, bool four);
  ~CoverStream();
  bool valid() const;
  bool push(const uint8_t* sourceRow, int y);
  bool complete() const { return current == height; }
 private:
  Print& out;
  int sourceWidth, width, height, stride, current = 0;
  bool four, failed = false;
  std::unique_ptr<int[]> bounds, rows;
  std::unique_ptr<uint32_t[]> sums;
  std::unique_ptr<uint8_t[]> packed;
  std::unique_ptr<AtkinsonDitherer> dither;
};

inline std::string cachePath(const std::string& cover, int w, int h, bool four) {
  const auto slot = cover.find("[HEIGHT]");
  if (slot == std::string::npos) return cover;
  const auto slash = cover.rfind('/', slot);
  return cover.substr(0, slash + 1) + "ink2_" + (four ? "4_" : "1_") + std::to_string(w) + "x" +
         std::to_string(h) + ".bmp";
}

constexpr uint8_t luma(uint8_t r, uint8_t g, uint8_t b) {
  return (19595u * r + 38470u * g + 7471u * b) >> 16;
}

// Centered BOX on encoded BT.601 Y, with no tone or gamma transform.
bool writeCover(const uint8_t* gray, int sw, int sh, Print& out, int w, int h, bool four);
}  // namespace tenorink
