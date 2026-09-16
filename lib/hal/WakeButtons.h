#pragma once

#include <cstdint>

namespace wakebuttons {
constexpr uint8_t POWER = 1u << 6;
constexpr uint8_t RIGHT_SIDE = 1u << 5;
constexpr uint8_t LEFT_SIDE = 1u << 4;
constexpr uint8_t allowedMask(uint8_t mode) {
  return mode == 1 ? POWER | RIGHT_SIDE : mode == 2 ? POWER | RIGHT_SIDE | LEFT_SIDE : mode == 3 ? 0x7f : POWER;
}

class HoldFilter {
  uint8_t released = 0;
  uint8_t holding = 0;
  uint8_t winner = 0;
  uint32_t started[7]{};

 public:
  uint8_t acceptedButton() const { return winner; }
  bool sample(uint8_t pressed, uint8_t allowed, uint32_t now) {
    released |= allowed & ~pressed;
    const uint8_t eligible = pressed & allowed & released;
    for (uint8_t index = 0; index < 7; ++index) {
      const uint8_t bit = 1u << index;
      if ((eligible & bit) == 0) {
        holding &= ~bit;
      } else if ((holding & bit) == 0) {
        holding |= bit;
        started[index] = now;
      } else if (static_cast<uint32_t>(now - started[index]) >= 400) {
        winner = bit;
        return true;
      }
    }
    return false;
  }
};
}  // namespace wakebuttons
