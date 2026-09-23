#pragma once
#include <cstdint>
// ButtonSymbols.cpp only reads three settings fields. This stands in for the
// real, disk-backed catalogue so the host test does not need JSON or SD I/O.
class CrossPointSettings {
 public:
  enum UI_THEME { TENOR_UI = 4 };
  enum { PREV_NEXT = 0, NEXT_PREV = 1 };
  uint8_t uiTheme = TENOR_UI;
  uint8_t tenorButtonSymbols = 1;
  uint8_t sideButtonLayout = PREV_NEXT;
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }
};
#define SETTINGS CrossPointSettings::getInstance()
