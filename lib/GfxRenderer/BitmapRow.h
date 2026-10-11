#pragma once

#include <cstdint>

namespace bitmaprow {
struct Target {
  uint8_t* data;
  int stride, width, height, origin, rows;
  int left, top, right, bottom;
  uint8_t ink[4];  // 0 leaves the bit, 1 clears it, 2 sets it.
  uint8_t dither[4][8]{};
};

// Each orientation fixes which axis walks bytes and which axis walks masks.
template <int Direction, bool Dither = false>
void write(const Target& t, const uint8_t* packed, const int16_t* columns, int from, int end, int y,
           bool transparent) {
  if (y < t.top || y >= t.bottom) return;
  constexpr bool vertical = Direction == 0 || Direction == 2;
  const int fixed = Direction == 0 || Direction == 3 ? y
      : (vertical ? t.width : t.height) - 1 - y;
  if constexpr (vertical) {
    if (fixed < 0 || fixed >= t.width) return;
  } else {
    if (fixed < t.origin || fixed >= t.origin + t.rows) return;
  }
  const int base = vertical ? fixed / 8 : (fixed - t.origin) * t.stride;
  const uint8_t rowMask = static_cast<uint8_t>(0x80 >> (fixed & 7));
  for (int i = from; i < end; ++i) {
    const int x = columns[i];
    if (x < 0 || x < t.left || x >= t.right) continue;
    const uint8_t level = (packed[i / 4] >> (6 - (i & 3) * 2)) & 3;
    uint8_t ink;
    if constexpr (Dither) ink = (t.dither[level][y & 7] & (0x80 >> (x & 7))) ? 1 : 2;
    else ink = t.ink[level];
    if (!ink || (transparent && level == 3)) continue;
    const int moving = Direction == 0 ? t.height - 1 - x : Direction == 1 ? t.width - 1 - x : x;
    int byte;
    uint8_t mask;
    if constexpr (vertical) {
      if (moving < t.origin || moving >= t.origin + t.rows) continue;
      byte = base + (moving - t.origin) * t.stride;
      mask = rowMask;
    } else {
      if (moving < 0 || moving >= t.width) continue;
      byte = base + moving / 8;
      mask = static_cast<uint8_t>(0x80 >> (moving & 7));
    }
    if (ink == 1) t.data[byte] &= static_cast<uint8_t>(~mask);
    else t.data[byte] |= mask;
  }
}
using Writer = void (*)(const Target&, const uint8_t*, const int16_t*, int, int, int, bool);
inline Writer writer(int direction, bool dither = false) {
  if (dither) {
    switch (direction) {
      case 0: return write<0, true>;
      case 1: return write<1, true>;
      case 2: return write<2, true>;
      default: return write<3, true>;
    }
  }
  switch (direction) {
    case 0: return write<0>;
    case 1: return write<1>;
    case 2: return write<2>;
    default: return write<3>;
  }
}
}  // namespace bitmaprow
