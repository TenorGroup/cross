#pragma once
#include <map>
#include "EpdFontFamily.h"
class SdCardFont;
class GfxRenderer {
 public:
  std::map<int,EpdFontFamily> fonts;
  std::map<int,SdCardFont*> sdFonts;
  std::map<int,int> fallbacks;
  const auto& getFontMap() const { return fonts; }
  void registerSdCardFont(int id,SdCardFont* font) { sdFonts[id] = font; }
  void insertFont(int id,EpdFontFamily family) { fonts.emplace(id,family); }
  void removeFont(int id) { fonts.erase(id); sdFonts.erase(id); }
  void clearFallbackFonts() { fallbacks.clear(); }
  void clearSdCardFonts() { sdFonts.clear(); }
};
