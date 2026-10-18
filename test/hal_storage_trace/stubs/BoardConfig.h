#pragma once

#include <cstdint>

#define FREEINK_SD_SDMMC 0

namespace BoardConfig {
enum class Board : uint8_t { XteinkX4, XteinkX3, XteinkX3Uc8279 };
struct DisplayPins {
  int8_t sclk, mosi, cs;
};
struct SdPins {
  int8_t sclk, miso, mosi, cs, powerEnable;
};
struct BoardProfile {
  Board board;
  DisplayPins display;
  SdPins sd;
};
// Pins as in the SDK's XTEINK_X3 / XTEINK_X4 profiles.
constexpr BoardProfile X3 = {Board::XteinkX3, {8, 10, 21}, {-1, 7, -1, 12, 13}};
constexpr BoardProfile X4 = {Board::XteinkX4, {8, 10, 21}, {-1, 7, -1, 12, -1}};
inline BoardProfile ACTIVE = X3;
}  // namespace BoardConfig
