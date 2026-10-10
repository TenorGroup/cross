#pragma once

#include <cstdint>

namespace freeink {
struct Icon {
  uint16_t w;
  uint16_t h;
  int16_t opticalCenterY;
  const uint8_t* bits;
};
}  // namespace freeink
