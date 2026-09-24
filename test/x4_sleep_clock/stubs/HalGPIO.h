#pragma once
#include <BoardConfig.h>
class HalGPIO {
 public:
  // isUsbConnected() is compiled from lib/hal/HalGPIO.cpp by gauge_task.py.
  bool lastUsbConnected = false;
  bool isUsbConnected() const;
  bool isXteinkDevice() const { return BoardConfig::ACTIVE.board != BoardConfig::Board::Other; }
  bool deviceIsX3() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::X3; }
  bool deviceIsX4() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::X4; }
  // Only the b2e808f sources call these two; they stay so journey.py
  // --source-root can still replay that tree against the same boundaries.
  uint8_t readWakeButtons() const { return 0; }
  static void markValidatedButtonWake(uint8_t) {}
};
