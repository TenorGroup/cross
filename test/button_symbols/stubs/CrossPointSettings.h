#pragma once
#include <cstdint>
// ButtonSymbols.cpp only reads three settings fields. This stands in for the
// real, disk-backed catalogue so the host test does not need JSON or SD I/O.
class CrossPointSettings {
 public:
  enum { PREV_NEXT = 0, NEXT_PREV = 1 };
  uint8_t uiTextSize = 0;  // TenorMenuChrome.h sizes its header and tab band from it
  uint8_t tenorButtonSymbols = 1;
  uint8_t tenorSideArrows = 1;
  uint8_t sideButtonLayout = PREV_NEXT;
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }
};
#define SETTINGS CrossPointSettings::getInstance()
