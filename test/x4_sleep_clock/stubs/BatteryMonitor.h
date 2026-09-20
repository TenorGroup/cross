#pragma once
#include <cstdint>
struct BatteryMonitor {
  bool readPercentageChecked(uint16_t& value) const { value = 80; return true; }
  uint16_t readPercentage() const { return 80; }
};
