#pragma once
#include <cstdint>
namespace BoardConfig {
enum class Board { X4, X3, Other };
enum class GaugeType { Bq27220, Cw2017, Axp2101 };
struct Profile {
  Board board = Board::X4;
  struct { uint8_t gaugeAddr = 0; GaugeType gaugeType = GaugeType::Bq27220; } batteryGauge;
  int batteryAdc = 0;
  int8_t usbDetect = -1;
  struct { int8_t latch0 = 13, latch1 = -1; } power;
  struct { int8_t power = 3; bool powerActiveHigh = false; } input;
  struct { int8_t rst = 6, powerEnable = -1; } display;
  struct { int8_t powerEnable = -1; bool powerActiveHigh = true; } sd;
  struct { int8_t powerEnable = -1; bool powerEnableActiveHigh = true; } touch;
  struct { int8_t enable = -1; bool enableActiveHigh = true; } mic;
};
inline Profile ACTIVE;
}
