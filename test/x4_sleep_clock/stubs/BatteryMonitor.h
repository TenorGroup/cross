#pragma once
#include <BoardConfig.h>
#include <cstdint>
// Counts what a production read would touch: the I2C gauge on a board that has one,
// the ADC divider (one millivolt conversion) on a board that does not.
namespace fakebattery {
inline int gaugeReads = 0, millivoltReads = 0;
// What the X3's BQ27220 Current() register answers: signed mA, positive while charging, or no answer.
inline int16_t current = 10;
inline bool readable = true;
// The next this-many gauge reads of Current() fail, whatever `readable` says (a bus glitch).
inline int failReads = 0;
// What the gauge's StateOfCharge answers, when a test wants it to move with the clock; unset it is 80.
inline uint16_t (*percentSource)() = nullptr;
}
struct BatteryMonitor {
  uint16_t readMillivolts() const {
    if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) ++fakebattery::gaugeReads;
    else ++fakebattery::millivoltReads;
    return 3900;
  }
  bool readPercentageChecked(uint16_t& value) const {
    readMillivolts();
    value = fakebattery::percentSource ? fakebattery::percentSource() : 80;
    return true;
  }
  uint16_t readPercentage() const { readMillivolts(); return 80; }
  // A gauge board with no charger IC (the X3) reads the gauge's own Current() sign, one I2C read; a failed read is false.
  bool isCharging() const {
    if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr == 0) return false;
    ++fakebattery::gaugeReads;
    if (fakebattery::failReads > 0) { --fakebattery::failReads; return false; }
    return fakebattery::readable && fakebattery::current > 0;
  }
  // The X3 has no charger IC that reports its input rail and no detect pin: the answer is "no opinion".
  bool isExternalPowerPresent(bool* known = nullptr) const {
    if (known) *known = false;
    return false;
  }
  static bool configureCharger() { return false; }  // boards with a configurable charger only
};
