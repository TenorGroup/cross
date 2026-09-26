#pragma once

#include <cstddef>
#include <cstdint>

// Bus side of the fake QMI8658 in harness.cpp: every transaction goes to the chip model.
class TwoWire {
 public:
  bool begin(int sda, int scl, uint32_t hz);
  void beginTransmission(uint8_t addr);
  size_t write(uint8_t value);
  uint8_t endTransmission(bool stop = true);
  uint8_t requestFrom(uint8_t addr, uint8_t len, uint8_t stop);
  int read();
};

extern TwoWire Wire;
