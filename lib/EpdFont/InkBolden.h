#pragma once

#include <cstdint>

namespace inkBolden {
struct Strength {
  uint8_t horizontal;
  uint8_t vertical;
};

// Total growth in half pixels per axis; tune on the real panel.
constexpr Strength strengths[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};

inline int floorDiv2(int value) { return value >= 0 ? value / 2 : -((-value + 1) / 2); }

// Levels 0..5. Returns false, untouched, for level 0 or an empty/invalid glyph.
inline bool apply(uint8_t* bitmap, int width, int height, bool twoBit, uint8_t level, bool antiAliased) {
  if (!bitmap || width <= 0 || height <= 0 || width > 255 || height > 255 || level == 0 || level > 5) return false;
  const int bits = twoBit ? 2 : 1;
  const int valueMask = (1 << bits) - 1;
  uint8_t line[256];
  auto read = [&](int index) {
    const int offset = index * bits;
    return (bitmap[offset >> 3] >> (8 - bits - (offset & 7))) & valueMask;
  };
  auto write = [&](int index, int value) {
    const int offset = index * bits;
    const int shift = 8 - bits - (offset & 7);
    bitmap[offset >> 3] = static_cast<uint8_t>((bitmap[offset >> 3] & ~(valueMask << shift)) | (value << shift));
  };
  for (int axis = 0; axis < 2; ++axis) {
    const int halfSteps = axis == 0 ? strengths[level].horizontal : strengths[level].vertical;
    const int left = twoBit ? halfSteps / 2 : halfSteps / 4;
    const int right = twoBit ? (halfSteps + 1) / 2 : (halfSteps + 2) / 4;
    const int length = axis == 0 ? width : height;
    const int count = axis == 0 ? height : width;
    for (int outer = 0; outer < count; ++outer) {
      auto index = [&](int position) { return axis == 0 ? outer * width + position : position * width + outer; };
      for (int position = 0; position < length; ++position) line[position] = static_cast<uint8_t>(read(index(position)));
      auto sample = [&](int position) { return position >= 0 && position < length ? line[position] : 0; };
      for (int position = 0; position < length; ++position) {
        int candidate = 0;
        for (int shift = -left; shift <= right; ++shift) {
          if (shift == 0) continue;
          int value;
          if (!twoBit) {
            value = sample(position - shift);
          } else if (shift % 2 == 0) {
            value = sample(position - floorDiv2(shift));
          } else {
            value = (sample(position - floorDiv2(shift - 1)) + sample(position - floorDiv2(shift + 1)) + 1) >> 1;
          }
          if (value > candidate) candidate = value;
        }
        if (twoBit && !antiAliased && candidate < 2) candidate = 0;
        write(index(position), candidate > line[position] ? candidate : line[position]);
      }
    }
  }
  return true;
}
}
