#pragma once
#include <algorithm>
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
  void registerBuiltinRaster(int id,SdCardFont* font) { registerSdCardFont(id,font); }
  bool replaceBuiltinFont(int id,const EpdFontFamily& family) {
    auto found=fonts.find(id);
    if(found==fonts.end())return false;
    found->second=family;
    return true;
  }
  void insertFont(int id,EpdFontFamily family) { fonts.emplace(id,family); }
  void removeFont(int id) { fonts.erase(id); sdFonts.erase(id); }
  void clearFallbackFonts() { fallbacks.clear(); }
  bool hasFallbackFont(int id) const {
    return std::any_of(fallbacks.begin(), fallbacks.end(),
                       [id](const auto& entry) { return entry.second == id; });
  }
  void clearSdCardFonts() { sdFonts.clear(); }
};
