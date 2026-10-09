#pragma once
#include <cstdint>
class CrossPointSettings {
 public:
  enum { CLOCK_HEADER_HIDE = 0, CLOCK_HEADER_TIME = 1, CLOCK_HEADER_TIME_DATE = 2 };
  uint8_t uiUglyLevel = 0;
  uint8_t clockShowInHeader = CLOCK_HEADER_HIDE;
  uint8_t clockFormat = 0;
  uint8_t uglyBatteryHidden = 1;
  bool globalStatusBarHidden() const { return true; }
  static CrossPointSettings& getInstance() {
    static CrossPointSettings settings;
    return settings;
  }
};
#define SETTINGS CrossPointSettings::getInstance()
