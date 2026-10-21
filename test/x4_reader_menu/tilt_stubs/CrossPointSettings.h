#pragma once
#include <cstdint>
struct CrossPointSettings {
  uint8_t tiltPageTurn = 0, tiltTabNavigation = 0, tiltMenuNavigation = 0;
  char sdFontFamilyName[32] = {};
  int saves = 0;
  void saveToFile() { ++saves; }
};
extern CrossPointSettings SETTINGS;
