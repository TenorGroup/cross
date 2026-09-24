#pragma once
#include <cstdint>
#include <cstring>
struct CrossPointSettings {
  char sdFontFamilyName[32] = "";
  uint8_t fontPointSize = 14;
  uint8_t readerInkWeight = 0;
  int (*sdFontIdResolver)(void*, const char*, uint8_t) = nullptr;
  void* sdFontResolverCtx = nullptr;
  int saves = 0;
  void saveToFile() { ++saves; }
  void clearSdFontFamily() {
    sdFontFamilyName[0] = '\0';
    saveToFile();
  }
};
inline CrossPointSettings hostSettings;
#define SETTINGS hostSettings
