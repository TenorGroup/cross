#pragma once
#include <BoardConfig.h>
class HalGPIO {
 public:
  bool isXteinkDevice() const { return BoardConfig::ACTIVE.board != BoardConfig::Board::Other; }
  bool deviceIsX3() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::X3; }
  bool deviceIsX4() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::X4; }
  uint8_t readWakeButtons() const { return 0; }
  static void markValidatedButtonWake(uint8_t) {}
};
