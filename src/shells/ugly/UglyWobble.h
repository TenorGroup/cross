#pragma once

#include <cstdint>

namespace ugly::logic {

inline int wobble(const uint32_t seed, const int index, const int range) {
  uint32_t value = seed * 2654435761u + static_cast<uint32_t>(index) * 40503u + 12345u;
  value ^= value >> 15;
  value *= 2246822519u;
  value ^= value >> 13;
  return static_cast<int>(value % static_cast<uint32_t>(2 * range + 1)) - range;
}

}
