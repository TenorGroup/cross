#pragma once
#include <BoardConfig.h>
#include <cstdint>
// Counts what a production read would touch: the I2C gauge on a board that has one,
// the ADC divider (one millivolt conversion) on a board that does not.
namespace fakebattery {
inline int gaugeReads = 0, millivoltReads = 0;
}
struct BatteryMonitor {
  uint16_t readMillivolts() const {
    if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) ++fakebattery::gaugeReads;
    else ++fakebattery::millivoltReads;
    return 3900;
  }
  bool readPercentageChecked(uint16_t& value) const { readMillivolts(); value = 80; return true; }
  uint16_t readPercentage() const { readMillivolts(); return 80; }
  bool isCharging() const { return false; }
};
