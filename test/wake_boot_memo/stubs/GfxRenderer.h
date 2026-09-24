#pragma once
#include <cstdint>
#include <map>
// Only what SdCardFontSystem asks of the renderer: whether the loaded reader font covers CJK,
// and which UI face falls back to which SD font.
struct HostFontFace {
  bool cjk = false;
  bool hasCodepoint(uint32_t cp) const { return cjk && cp >= 0x3000; }
};
class GfxRenderer {
 public:
  std::map<int, HostFontFace> fonts;
  std::map<int, int> fallbacks;
  const std::map<int, HostFontFace>& getFontMap() const { return fonts; }
  void setFallbackFont(int uiId, int sdId) { fallbacks[uiId] = sdId; }
};
