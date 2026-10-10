#pragma once

#include <cstdint>
#include <string>

class Print;

#ifndef TENOR_INK_HOME_LEVELS
#define TENOR_INK_HOME_LEVELS 4
#endif

namespace tenorink {
// The single Home gray policy, including the emergency monochrome switch.
constexpr bool homeGray(bool touch, bool psram, bool recent, bool ugly, bool absoluteSupported,
                        int levels = TENOR_INK_HOME_LEVELS) {
  return touch && psram && recent && !ugly && absoluteSupported && levels == 4;
}

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
