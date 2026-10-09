#pragma once
#include <cstdint>
#include <cstring>
struct CrossPointSettings {
  enum { NOTOSERIF = 0, NOTOSANS = 1 };
  uint8_t fontFamily = NOTOSERIF;
  char sdFontFamilyName[64] = "";
  uint8_t fontPointSize = 14;
  uint8_t readerInkWeight = 0;
  int (*sdFontIdResolver)(void*, const char*, uint8_t) = nullptr;
  void* sdFontResolverCtx = nullptr;
  int saves = 0;
  int getReaderFontId() const { return 1000 + fontFamily * 100 + fontPointSize; }
  void saveToFile() { ++saves; }
  void clearSdFontFamily() {
    sdFontFamilyName[0] = '\0';
    saveToFile();
  }
};
inline CrossPointSettings hostSettings;
#define SETTINGS hostSettings
