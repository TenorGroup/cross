#pragma once
#include <cstdint>
struct SPISettings { SPISettings() {} SPISettings(uint32_t, uint8_t, uint8_t) {} };
class SPIClass {
 public:
  void begin() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t) { return 0; }
};
extern SPIClass SPI;
