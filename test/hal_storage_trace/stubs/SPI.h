#pragma once

#include <HwLog.h>

struct SPIClass {
  void end() { hwLog.push_back("SPI.end"); }
};
inline SPIClass SPI;
